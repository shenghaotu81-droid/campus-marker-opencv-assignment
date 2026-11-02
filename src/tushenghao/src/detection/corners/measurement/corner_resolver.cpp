// 输入当前帧、已建立对应和模型；编排四个原图角取证，四角均成功才返回测量。
// 旧最近白块fallback和低分辨率轮廓拟合已删除；模型投影只传给搜索/验证。
#include "corners/measurement/corner_resolver.hpp"
#include "corners/observation/corner_observation.hpp"
#include "corners/fitting/corner_edge_fit.hpp"
#include "detector/corner_budget_validation.hpp"
#include "vision/math/observed_geometry.hpp"
#include <map>
#include <set>

namespace mark
{
    CornerResolution resolveObservedCorners(const PreparedFrame &frame,
                                            const GeometryHypothesis &hypothesis,
                                            const MarkerGeometry &model, const CornerConfig &config)
    {
        // 原 wrapper 每次创建新索引，不能因相同帧号/组件 ID 复用已修改的输入。
        validateCornerObservationBudget(config);
        OriginalContourIndex index(frame, frame.frame_id_, *config.observation_budget_);
        return resolveObservedCorners(frame, frame.components_, hypothesis, model, config, index.source());
    }

    CornerResolution resolveObservedCorners(const PreparedFrame &frame,
                                            const std::vector<WhiteComponent> &observations,
                                            const GeometryHypothesis &hypothesis,
                                            const MarkerGeometry &model, const CornerConfig &config,
                                            const OriginalContourSource &source, ScreenOrder *screen_order)
    {
        validateCornerObservationBudget(config);
        // 来源绑定显式用 frame ID 检查；索引生命周期由 decode 的局部对象限定。
        if (source.frame_id != frame.frame_id_)
        {
            return cornerFailure("ORIGINAL_INDEX_FRAME_MISMATCH");
        }
        // 格式/映射非法与图像缺边分开登记，严禁除零或访问非法affine。
        if (frame.original_image_.empty() || frame.original_image_.dims != 2 ||
            frame.original_image_.type() != CV_8UC3 || frame.image_.empty() ||
            frame.image_.dims != 2 || frame.image_.type() != CV_8UC3 ||
            !std::isfinite(frame.scale_x_) || !std::isfinite(frame.scale_y_) ||
            frame.scale_x_ <= 0 || frame.scale_y_ <= 0 || frame.original_white_threshold_ < 0 ||
            frame.original_white_threshold_ > 255 ||
            !observed::validAffine(hypothesis.affine_transform_))
        {
            return cornerFailure("INVALID_CORNER_INPUT: 原图/映射/非镜像仿射非法");
        }
        // 显式映射必须和真实resize尺寸吻合，不能让任意正scale把搜索域错映后“成功”。
        if (frame.scale_x_ != static_cast<double>(frame.image_.cols) / frame.original_image_.cols ||
            frame.scale_y_ != static_cast<double>(frame.image_.rows) / frame.original_image_.rows)
        {
            return cornerFailure("INVALID_COORDINATE_MAPPING");
        }
        if (hypothesis.completeness_ == GeometryCompleteness::CLEARLY_INCOMPLETE)
        {
            return cornerFailure("CLEARLY_INCOMPLETE: 父假设已明确不完整");
        }
        std::map<size_t, const WhiteComponent *> components;
        for (const auto &c : observations)
            if (!components.emplace(c.component_id_, &c).second)
            {
                return cornerFailure("DUPLICATE_COMPONENT_ID");
            }
        std::map<std::string, size_t> assignments;
        std::set<size_t> used;
        for (const auto &a : hypothesis.assignments_)
        {
            if (!assignments.emplace(a.model_part_id_, a.component_id_).second ||
                !used.insert(a.component_id_).second || !components.count(a.component_id_))
            {
                return cornerFailure("INVALID_ASSIGNMENT: 重复身份/一片多身份/悬空引用");
            }
        }

        // D11：读取唯一的只读物理角身份契约，替换拟合不需同步维护绑定表。
        const auto &bindings = model_corner_contract;
        CornerMeasurement measurement{};
        for (int i = 0; i < 4; ++i)
        {
            const auto &binding = bindings[i];
            auto assignment = assignments.find(binding.part);
            if (assignment == assignments.end())
            {
                return cornerFailure(i == 1 ? "MISSING_M_ASSIGNMENT" : "MISSING_L_ASSIGNMENT");
            }
            const GeometryPolygon *polygon = nullptr;
            for (const auto &p : model.polygons)
                if (p.id == binding.part)
                    polygon = &p;
            if (!polygon ||
                polygon->vertices.size() <=
                    static_cast<size_t>(std::max({binding.vertex, binding.a, binding.b})))
            {
                return cornerFailure("INVALID_CORNER_BINDING");
            }
            // 多个父假设的同一组件共享关联结果；用引用保留完整原始轮廓而不反复复制。
            const auto &contour = source.observe(*components.at(assignment->second));
            if (!contour.reason.empty())
            {
                return cornerFailure(contour.reason + ": P" + std::to_string(i));
            }
            std::array<std::array<cv::Point2d, 2>, 2> edges;
            int edge_ids[] = {binding.a, binding.b};
            for (int e = 0; e < 2; ++e)
                for (int end = 0; end < 2; ++end)
                {
                    size_t vertex = (edge_ids[e] + end) % polygon->vertices.size();
                    edges[e][end] = frame.workToOriginal(
                        observed::project(hypothesis.affine_transform_, polygon->vertices[vertex]));
                    if (!observed::finite(edges[e][end]))
                    {
                        return cornerFailure("INVALID_MODEL_PROJECTION");
                    }
                }
            auto fit = fitObservedEdgePair(contour.contour, edges, config);
            if (!fit.evidence)
            {
                return cornerFailure(fit.reason + ": P" + std::to_string(i));
            }
            auto &evidence = *fit.evidence;
            evidence.physical_corner_ = static_cast<PhysicalCorner>(i);
            evidence.frame_id_ = frame.frame_id_;
            evidence.component_id_ = assignment->second;
            evidence.model_edge_ids_ = {binding.a, binding.b};
            evidence.model_vertex_id_ = binding.vertex;
            evidence.stable_id_ = std::to_string(frame.frame_id_) + "/" +
                                  std::to_string(assignment->second) + "/" +
                                  std::to_string(binding.a) + ":" +
                                  std::to_string(evidence.observed_segment_ids_[0]) + ":" +
                                  std::to_string(evidence.observed_segment_end_ids_[0]) + "/" +
                                  std::to_string(binding.b) + ":" +
                                  std::to_string(evidence.observed_segment_ids_[1]) + ":" +
                                  std::to_string(evidence.observed_segment_end_ids_[1]);
            measurement.physical_corners_[i] = evidence.intersection_;
            measurement.evidence_[i] = std::move(evidence);
        }
        const auto ordered = orderScreenCorners(measurement.physical_corners_,config);
        if (!ordered.screen_order_) return cornerFailure("INVALID_MEASURED_QUADRILATERAL");
        if (screen_order) *screen_order = *ordered.screen_order_;
        return cornerSuccess(std::move(measurement));
    }
}
