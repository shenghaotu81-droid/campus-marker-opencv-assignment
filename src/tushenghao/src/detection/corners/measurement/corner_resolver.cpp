#include "corners/measurement/corner_resolver.hpp"
#include "corners/observation/corner_observation.hpp"
#include "corners/fitting/corner_edge_fit.hpp"
#include "detector/corner_budget_validation.hpp"
#include "vision/math/observed_geometry.hpp"
#include <set>

namespace mark
{
CornerResolution resolveObservedCorners(const PreparedFrame &frame, const MatchQuad &quad,
                                        const CornerConfig &config, OriginalContourIndex &index,
                                        ScreenOrder *order)
{
    validateCornerObservationBudget(config);
    if (index.frameId() != frame.frame_id_)
        return cornerFailure("ORIGINAL_INDEX_FRAME_MISMATCH");
    if (frame.original_image_.empty() || frame.original_image_.type() != CV_8UC3 ||
        frame.original_image_.dims != 2 || frame.image_.empty() ||
        frame.image_.type() != CV_8UC3 || frame.image_.dims != 2 ||
        frame.scale_x_ != double(frame.image_.cols) / frame.original_image_.cols ||
        frame.scale_y_ != double(frame.image_.rows) / frame.original_image_.rows ||
        frame.original_white_threshold_ < 0 || frame.original_white_threshold_ > 255)
        return cornerFailure("INVALID_CORNER_INPUT");
    if (!orderScreenCorners(quad, config).screen_order_)
        return cornerFailure("INVALID_QUAD");

    CornerMeasurement measurement{};
    std::set<size_t> used;
    for (size_t i = 0; i < 4; ++i)
    {
        const auto original = index.atCorner(quad[i]);
        if (!original.reason.empty())
            return cornerFailure(original.reason + ": P" + std::to_string(i));
        if (!used.insert(original.component_id).second)
            return cornerFailure("REUSED_CORNER_COMPONENT: P" + std::to_string(i));
        // 仅使用相邻粗顶点连线作搜索提示；精角是原图两条拟合线的交点。
        const std::array<std::array<cv::Point2d, 2>, 2> edges{{
            {quad[(i + 3) % 4], quad[i]}, {quad[i], quad[(i + 1) % 4]}}};
        auto fit = fitObservedEdgePair(original.contour, edges, config);
        if (!fit.evidence)
            return cornerFailure(fit.reason + ": P" + std::to_string(i));
        auto &evidence = *fit.evidence;
        evidence.physical_corner_ = PhysicalCorner(i);
        evidence.frame_id_ = frame.frame_id_;
        evidence.component_id_ = original.component_id;
        measurement.physical_corners_[i] = evidence.intersection_;
        measurement.evidence_[i] = std::move(evidence);
    }
    const auto sorted = orderScreenCorners(measurement.physical_corners_, config);
    if (!sorted.screen_order_)
        return cornerFailure("INVALID_MEASURED_QUADRILATERAL");
    if (order)
        *order = *sorted.screen_order_;
    return cornerSuccess(std::move(measurement));
}
} // namespace mark
