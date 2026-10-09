// 只补缺失M/S并独立校验已有绑定；真实边界/拓扑/面积共同供身份，不凭最近中心。
#include "geometry/geometry_assignment_completion.hpp"
#include "geometry/assignment_match_metrics.hpp"
#include "core/config_error.hpp"
#include <opencv2/imgproc.hpp>
#include <map>
#include <set>
#include <functional>

namespace mark
{
    namespace
    {
        // 原始指标共用测量入口；正式路径在此消费显式预算，C不会先按上限删样本。
        bool compatible(const WhiteComponent &c, const GeometryPolygon &part, const cv::Mat &affine,
                        double reference, double model_reference,
                        const AssignmentCompletionConfig &budget)
        {
            // 正式匹配先按独立面积约束快拒绝；C原始测量直接调用helper，不先过滤尾部。
            if (!std::isfinite(c.area_) || c.area_ <= 0 ||
                std::abs(c.area_ / reference - part.area / model_reference) >
                    budget.max_relative_area_error)
                return false;
            auto m = measureAssignmentMatch(c, part, affine, reference, model_reference,
                                            budget.approximation_epsilon_work_px,
                                            budget.boundary_sample_step_work_px);
            return m.valid && m.topology_valid &&
                   m.relative_area_error <= budget.max_relative_area_error &&
                   m.boundary_distance <= budget.max_boundary_distance_work_px &&
                   m.direction_diff <= budget.max_direction_diff_deg;
        }
    }

    GeometryBatch completeSegmentedAssignments(const GeometryBatch &input,
                                               const std::vector<WhiteComponent> &components,
                                               const MarkerGeometry &model,
                                               const AssignmentCompletionConfig &config)
    {
        for (double v : {config.max_boundary_distance_work_px, config.approximation_epsilon_work_px,
                         config.max_direction_diff_deg, config.max_relative_area_error,
                         config.boundary_sample_step_work_px})
            if (!std::isfinite(v) || v < 0)
                throw ConfigError("ASSIGNMENT_CONFIG_INVALID: 非负有限预算必需");
        if (config.approximation_epsilon_work_px <= 0 || config.boundary_sample_step_work_px <= 0 ||
            config.max_direction_diff_deg > 90 || !config.max_candidates ||
            !config.max_expansions || !config.max_output_branches)
            throw ConfigError("ASSIGNMENT_CONFIG_INVALID: 简化/采样/资源上限非法");
        GeometryBatch output;
        output.segmented_assignments_ready_ = true;
        output.resource_truncated_ = input.resource_truncated_;
        output.diagnostics_ = input.diagnostics_;
        std::map<size_t, const WhiteComponent *> by_id;
        std::map<std::string, const GeometryPolygon *> parts;
        for (const auto &c : components)
            if (!by_id.emplace(c.component_id_, &c).second)
                throw std::invalid_argument("DUPLICATE_COMPONENT_ID");
        for (const auto &p : model.polygons)
            if (!parts.emplace(p.id, &p).second)
                throw std::invalid_argument("DUPLICATE_MODEL_ID");
        for (const auto &id : {"L0", "L2", "L3", "M1", "S1a", "S1b"})
            if (!parts.count(id))
                throw std::invalid_argument("MISSING_MODEL_PART");
        size_t extensions = 0, examined_total = 0;
        bool exhausted = false;
        for (size_t parent = 0; parent < input.hypotheses_.size(); ++parent)
        {
            const auto &hypothesis = input.hypotheses_[parent];
            auto reject = [&](const char *reason)
            {
                output.diagnostics_.push_back("assignment/" + std::to_string(parent) + "/" +
                                              reason);
            };
            if (hypothesis.completeness_ == GeometryCompleteness::CLEARLY_INCOMPLETE ||
                !observed::validAffine(hypothesis.affine_transform_) ||
                !std::isfinite(hypothesis.validation_residual_) ||
                hypothesis.validation_residual_ < 0)
            {
                reject("INVALID_PARENT");
                continue;
            }
            std::map<std::string, size_t> assignments;
            std::set<size_t> used;
            bool valid = true;
            for (const auto &a : hypothesis.assignments_)
                if (!parts.count(a.model_part_id_) || !by_id.count(a.component_id_) ||
                    !assignments.emplace(a.model_part_id_, a.component_id_).second ||
                    !used.insert(a.component_id_).second)
                    valid = false;
            std::vector<double> areas, model_areas;
            for (const auto &id : {"L0", "L2", "L3"})
            {
                if (!assignments.count(id))
                {
                    valid = false;
                    break;
                }
                const auto &c = *by_id.at(assignments.at(id));
                if (!std::isfinite(c.area_) || c.area_ <= 0 || c.touches_border_)
                {
                    valid = false;
                    break;
                }
                areas.push_back(c.area_);
                model_areas.push_back(parts.at(id)->area);
            }
            if (!valid)
            {
                reject("INVALID_PARENT_ASSIGNMENT");
                continue;
            }
            std::sort(areas.begin(), areas.end());
            std::sort(model_areas.begin(), model_areas.end());
            std::vector<std::string> missing;
            std::vector<std::vector<size_t>> candidates;
            for (const auto &id : {"M1", "S1a", "S1b"})
            {
                if (assignments.count(id))
                {
                    if (!compatible(*by_id.at(assignments.at(id)), *parts.at(id),
                                    hypothesis.affine_transform_, areas[1], model_areas[1], config))
                        valid = false;
                    continue;
                }
                missing.emplace_back(id);
                candidates.emplace_back();
                size_t examined = 0;
                for (const auto &item : by_id)
                {
                    if (used.count(item.first))
                        continue;
                    if (examined == config.max_candidates)
                    {
                        output.resource_truncated_ = true;
                        break;
                    }
                    ++examined;
                    ++examined_total;
                    if (compatible(*item.second, *parts.at(id), hypothesis.affine_transform_,
                                   areas[1], model_areas[1], config))
                        candidates.back().push_back(item.first);
                }
            }
            if (!valid)
            {
                reject("INVALID_EXISTING_SEGMENTED_BINDING");
                continue;
            }
            // 已有六片通过同样验证后保持原内容，不重复追加证据也不计新增搜索。
            if (missing.empty())
            {
                if (output.hypotheses_.size() == config.max_output_branches)
                {
                    output.resource_truncated_ = true;
                    exhausted = true;
                }
                else
                    output.hypotheses_.push_back(hypothesis);
                if (exhausted)
                    break;
                continue;
            }
            GeometryHypothesis branch = hypothesis;
            std::function<void(size_t)> visit = [&](size_t depth)
            {
                if (depth == missing.size())
                {
                    if (output.hypotheses_.size() == config.max_output_branches)
                    {
                        output.resource_truncated_ = true;
                        exhausted = true;
                        return;
                    }
                    output.hypotheses_.push_back(branch);
                    return;
                }
                for (auto id : candidates[depth])
                {
                    if (used.count(id))
                        continue;
                    // 上限处仍有真正待扩展分支才标截断，K恰好穷尽不误报。
                    if (extensions == config.max_expansions)
                    {
                        output.resource_truncated_ = true;
                        exhausted = true;
                        return;
                    }
                    ++extensions;
                    used.insert(id);
                    branch.assignments_.push_back({missing[depth], id});
                    branch.evidence_.push_back("assignment:" + missing[depth] +
                                               "/component:" + std::to_string(id));
                    visit(depth + 1);
                    branch.evidence_.pop_back();
                    branch.assignments_.pop_back();
                    used.erase(id);
                    if (exhausted)
                        return;
                }
            };
            size_t previous = output.hypotheses_.size();
            visit(0);
            if (previous == output.hypotheses_.size())
                reject("SEGMENTED_SUPPORT_INSUFFICIENT");
            // 真有下一合法分支但预算已耗尽时，继续扫描其余父假设没有可交付收益。
            // 与“恰好穷尽”不同，只在以上实际未展开/未输出分支触发后停止。
            if (exhausted)
                break;
        }
        output.diagnostics_.push_back("assignment/expansions=" + std::to_string(extensions));
        output.diagnostics_.push_back("assignment/candidates_examined=" +
                                      std::to_string(examined_total));
        output.diagnostics_.push_back("assignment/output_branches=" +
                                      std::to_string(output.hypotheses_.size()));
        return output;
    }
}
