// 旧Validator只看凸四点即可放行；这里要求四角真实来源并复算证据，失败给明确原因。
#include "corners/corner_evidence_validation.hpp"
#include "corners/periodic_line_fit.hpp"
#include "core/observed_geometry_utils.hpp"
#include <set>

namespace mark
{
    namespace
    {
        // 机器精度比较只校验同一计算结果的一致性，不替代任何工程定位预算。
        bool same(double a, double b)
        {
            return std::isfinite(a) && std::isfinite(b) &&
                   std::abs(a - b) <= 64 * std::numeric_limits<double>::epsilon() *
                                          std::max({1.0, std::abs(a), std::abs(b)});
        }
    }

    bool validateCornerEvidence(const CornerEvidence &e, cv::Point2d corner, int index,
                                const CornerConfig &config, std::string &reason)
    {
        auto fail = [&](const char *text)
        {
            reason = text;
            return false;
        };
        if (!config.observation_budget_)
            return fail("CORNER_BUDGET_NOT_CONFIGURED");
        if (e.truncated_)
            return fail("TRUNCATED_EVIDENCE: 不能发布截断角");
        const std::array<std::array<int, 2>, 4> edge_ids{{{5, 0}, {0, 1}, {5, 0}, {5, 0}}};
        if (!e.original_observation_ || e.stable_id_.empty() ||
            e.component_id_ == std::numeric_limits<size_t>::max() ||
            static_cast<int>(e.physical_corner_) != index ||
            e.model_vertex_id_ != (index == 1 ? 1 : 0) || e.model_edge_ids_ != edge_ids[index] ||
            e.observed_segment_ids_.size() != 2 || e.observed_segment_ids_[0] < 0 ||
            e.observed_segment_ids_[1] < 0 ||
            e.observed_segment_ids_[0] == e.observed_segment_ids_[1] ||
            e.observed_segment_end_ids_[0] < 0 || e.observed_segment_end_ids_[1] < 0)
            return fail("MISSING_CORNER_EVIDENCE: 来源/物理绑定/独立弧非法");
        if (!same(e.intersection_.x, corner.x) || !same(e.intersection_.y, corner.y))
            return fail("EVIDENCE_CORNER_MISMATCH");
        const cv::Vec4d lines[] = {e.line_a_, e.line_b_};
        for (int edge = 0; edge < 2; ++edge)
        {
            const auto &line = lines[edge];
            const auto &support = e.original_support_arcs_[edge];
            for (double value : line.val)
                if (!std::isfinite(value))
                    return fail("INVALID_FIT_LINE");
            double norm = std::hypot(line[0], line[1]);
            if (!(norm > 0))
                return fail("DEGENERATE_FIT_LINE");
            cv::Point2d direction{line[0] / norm, line[1] / norm}, origin{line[2], line[3]};
            std::set<std::pair<double, double>> unique;
            double sum = 0, maximum = 0, lo = INFINITY, hi = -INFINITY;
            double model_sum = 0, model_maximum = 0;
            const auto kind = e.line_model_kinds_[edge];
            // 周期参数不能相信调用方；从完整有序支持重建估计器，拒绝模型/轴/c/物理线篡改。
            if (kind != LineFitModel::Legacy)
            {
                auto rebuilt = fitPeriodicLine(support, kind);
                if (!rebuilt || rebuilt->axis != e.periodic_axes_[edge] ||
                    !same(rebuilt->coefficient, e.periodic_coefficients_[edge]))
                    return fail("PERIODIC_MODEL_MISMATCH");
                const double rebuilt_norm = std::hypot(rebuilt->line[0], rebuilt->line[1]);
                rebuilt->line[0] /= rebuilt_norm;
                rebuilt->line[1] /= rebuilt_norm;
                for (size_t parameter = 0; parameter < 4; ++parameter)
                    if (!same(line[parameter], rebuilt->line[parameter]))
                        return fail("PERIODIC_LINE_MISMATCH");
            }
            for (size_t pixel = 0; pixel < support.size(); ++pixel)
            {
                auto p = support[pixel];
                // CHAIN_APPROX_NONE像素弧必须连续，不能把远处同线点拼成支持段。
                if (pixel &&
                    (cv::norm(p - support[pixel - 1]) > std::sqrt(2.0) || p == support[pixel - 1]))
                    return fail("DISCONTINUOUS_SUPPORT");
                if (!observed::finite(p))
                    return fail("NONFINITE_SUPPORT");
                unique.emplace(p.x, p.y);
                double t = (p - corner).dot(direction);
                lo = std::min(lo, t);
                hi = std::max(hi, t);
                double d =
                    std::abs(direction.x * (p.y - origin.y) - direction.y * (p.x - origin.x));
                sum += d;
                maximum = std::max(maximum, d);
                const cv::Vec4d normalized{direction.x, direction.y, origin.x, origin.y};
                const double model_distance =
                    kind == LineFitModel::Legacy
                        ? d
                        : periodicLineDistance(p, normalized, e.periodic_axes_[edge],
                                               e.periodic_coefficients_[edge]);
                model_sum += model_distance;
                model_maximum = std::max(model_maximum, model_distance);
            }
            if (unique.size() < static_cast<size_t>(config.min_line_points_) ||
                hi - lo < config.observation_budget_->min_support_span_px)
                return fail("SHORT_SUPPORT: 去重点或跨度不足");
            auto segment = edge == 0 ? e.edge_segment_a_ : e.edge_segment_b_;
            if (segment[0] != support.front() || segment[1] != support.back())
                return fail("SEGMENT_SUPPORT_MISMATCH");
            double mean = sum / support.size(), extension = lo > 0 ? lo : hi < 0 ? -hi : 0;
            // raw 与校正残差分别复算；原 0.5 只约束所声明模型的平均垂距，旧字段定义不变。
            const double model_mean = model_sum / support.size();
            if (kind != LineFitModel::Legacy &&
                (!same(model_mean, e.model_mean_residual_px_[edge]) ||
                 !same(model_maximum, e.model_max_residual_px_[edge])))
                return fail("PERIODIC_RESIDUAL_MISMATCH");
            if (!same(mean, e.line_mean_residual_px_[edge]) ||
                !same(maximum, e.line_max_residual_px_[edge]) ||
                !same(extension, e.support_extension_px_[edge]) ||
                model_mean > config.max_line_fit_error_ ||
                extension > config.observation_budget_->max_support_extension_px)
                return fail("INVALID_EVIDENCE_ERROR: 误差/有限支持延伸不合格");
            // 无限线交点必须确实在两条拟合线上，不能只写一个任意凸四点。
            double line_distance =
                std::abs(direction.x * (corner.y - origin.y) - direction.y * (corner.x - origin.x));
            const double roundoff = 64 * std::numeric_limits<double>::epsilon() *
                                    std::max({1.0, std::abs(corner.x), std::abs(corner.y),
                                              std::abs(origin.x), std::abs(origin.y)});
            if (!std::isfinite(line_distance) || line_distance > roundoff)
                return fail("INTERSECTION_LINE_MISMATCH");
        }
        std::set<std::pair<double, double>> first_points;
        for (auto p : e.original_support_arcs_[0])
            first_points.emplace(p.x, p.y);
        for (auto p : e.original_support_arcs_[1])
            if (first_points.count({p.x, p.y}))
                return fail("REUSED_SUPPORT_ARC");
        double angle =
            observed::angleDeg({e.line_a_[0], e.line_a_[1]}, {e.line_b_[0], e.line_b_[1]});
        if (!std::isfinite(angle) || angle < config.min_intersection_angle_deg_)
            return fail("ILL_CONDITIONED_INTERSECTION");
        if (e.original_turn_arc_.size() < 2)
            return fail("MISSING_TURN_ARC");
        const auto &turn = e.original_turn_arc_;
        const auto &a = e.original_support_arcs_[0];
        const auto &b = e.original_support_arcs_[1];
        if (!((turn.front() == a.back() && turn.back() == b.front()) ||
              (turn.front() == b.back() && turn.back() == a.front())))
            return fail("TURN_SUPPORT_MISMATCH");
        double length = 0;
        for (size_t i = 0; i < e.original_turn_arc_.size(); ++i)
        {
            if (!observed::finite(e.original_turn_arc_[i]))
                return fail("NONFINITE_TURN_ARC");
            if (i)
            {
                double step = cv::norm(e.original_turn_arc_[i] - e.original_turn_arc_[i - 1]);
                if (step == 0 || step > std::sqrt(2.0))
                    return fail("DISCONTINUOUS_TURN_ARC");
                length += step;
            }
        }
        double corner_error = observed::boundaryDistance(corner, e.original_turn_arc_, false);
        if (length > config.observation_budget_->max_turn_connection_length_px ||
            !same(corner_error, e.corner_error_px_) || corner_error > config.max_corner_error_ ||
            !same(e.error_, e.line_mean_residual_px_[0] + e.line_mean_residual_px_[1]))
            return fail("INVALID_TURN_ERROR");
        return true;
    }
}
