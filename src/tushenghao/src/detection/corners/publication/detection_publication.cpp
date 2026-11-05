#include "vision/math/observed_geometry.hpp"
#include "corners/publication/detection_publication.hpp"
#include "corners/ordering/screen_order.hpp"
#include <algorithm>
#include <cmath>

namespace mark
{
    // 先按物理编号舍入再排序，避免丢掉P标签后猜映射；严格表示检查不添加像素门限。
    std::optional<Detection> publishFloatDetection(const CornerMeasurement &measurement,
                                                   bool orientation_unique, cv::Size size,
                                                   const CornerConfig &config, std::string &reason)
    {
        reason.clear();
        if (size.width <= 0 || size.height <= 0)
        {
            reason = "PUBLISH_INVALID_SIZE";
            return std::nullopt;
        }
        std::array<cv::Point2d, 4> rounded;
        for (size_t i = 0; i < 4; ++i)
        {
            const auto &source = measurement.physical_corners_[i];
            if (!std::isfinite(source.x) || !std::isfinite(source.y))
            {
                reason = "PUBLISH_NON_FINITE";
                return std::nullopt;
            }
            const cv::Point2f point(source);
            rounded[i] = point;
            if (!std::isfinite(point.x) || !std::isfinite(point.y))
            {
                reason = "PUBLISH_NON_FINITE";
                return std::nullopt;
            }
            if (point.x < 0 || point.y < 0 || point.x >= size.width || point.y >= size.height)
            {
                reason = "PUBLISH_OUT_OF_BOUNDS";
                return std::nullopt;
            }
            for (size_t j = 0; j < i; ++j)
                if (rounded[i] == rounded[j])
                {
                    reason = "PUBLISH_DUPLICATE";
                    return std::nullopt;
                }
        }
        double sign = 0;
        for (size_t i = 0; i < 4; ++i)
        {
            const double cross = (rounded[(i + 1) % 4] - rounded[i])
                                     .cross(rounded[(i + 2) % 4] - rounded[(i + 1) % 4]);
            if (!std::isfinite(cross) || cross == 0 || (i && ((cross > 0) != (sign > 0))))
            {
                reason = "PUBLISH_NON_CONVEX_OR_DEGENERATE";
                return std::nullopt;
            }
            sign = cross;
        }
        auto order = orderScreenCycle(rounded, config, reason);
        if (!order)
        {
            reason = "PUBLISH_SCREEN_REJECTED: " + reason;
            return std::nullopt;
        }
        Detection detection{};
        detection.category = MarkCategory::Unknown;
        for (size_t i = 0; i < 4; ++i)
            detection.corners[i] = cv::Point2f(order->screen_points[i]);
        for (const auto &e : measurement.evidence_) {
            if (e.corner_error_px_ > config.max_corner_error_) detection.quality_flags |= CornerErrorHigh;
            for (size_t edge=0;edge<2;++edge) {
                if (e.model_mean_residual_px_[edge] > config.max_line_fit_error_)
                    detection.quality_flags |= LineFitResidualHigh;
                if (config.observation_budget_ && e.support_extension_px_[edge] > config.observation_budget_->max_support_extension_px)
                    detection.quality_flags |= SupportExtensionHigh;
            }
        }
        detection.bbox = observed::four_corner_bounds(detection.corners);
        if (orientation_unique)
            detection.attributes.orientation = order->input_to_screen;
        return detection;
    }
}
