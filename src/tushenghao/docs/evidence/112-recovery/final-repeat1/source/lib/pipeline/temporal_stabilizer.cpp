#include "pipeline/temporal_stabilizer.hpp"
#include "pipeline/frame_sequence.hpp"
#include "corners/screen_order.hpp"
#include "config/config.hpp"
#include <cmath>
#include <stdexcept>
#include <algorithm>

namespace mark
{
    // 参考权重只定义时间常数；实际dt每帧计算，避免固定0.7依赖帧率。
    double computeTimeConstantSeconds(double dt, double alpha)
    {
        if (!std::isfinite(dt) || dt <= 0 || !std::isfinite(alpha) || alpha <= 0 || alpha >= 1)
            throw std::invalid_argument("INVALID_TIME_CONSTANT_PARAMETER");
        double tau = -(dt / 1000.0) / std::log1p(-alpha);
        if (!std::isfinite(tau) || tau <= 0)
            throw std::invalid_argument("TIME_CONSTANT_UNREPRESENTABLE");
        return tau;
    }

    double computeCurrentWeight(double dt, double tau)
    {
        if (!std::isfinite(dt) || dt <= 0 || !std::isfinite(tau) || tau <= 0)
            throw std::invalid_argument("INVALID_DT_OR_TAU");
        return -std::expm1(-dt / tau);
    }

    TemporalStabilizer::TemporalStabilizer(TemporalConfig c)
        : config_(c), tau_(computeTimeConstantSeconds(c.reference_dt_ms, c.reference_alpha))
    {
        AppConfig app;
        app.detector_config.temporal = c;
        validateConfig(app);
        if (c.stabilization_enabled &&
            (!c.correspondence_uncertainty_px || !c.max_smoothing_deviation_px))
            throw std::invalid_argument("TEMPORAL_BUDGET_MISSING");
    }

    void TemporalStabilizer::reset(ResetReason reason) noexcept
    {
        resets_.add(reason);
        reference_.reset();
        smoothing_.reset();
        last_input_.reset();
        size_.reset();
        diagnostics_ = {};
    }

    namespace
    {
        // 使用原始四边形面积和中心，而非bbox面积或质量标志作为排序分数。
        double area(const Detection &d)
        {
            double a = 0;
            for (size_t i = 0; i < 4; ++i)
                a += cv::Point2d(d.corners[i]).cross(cv::Point2d(d.corners[(i + 1) % 4]));
            return a / 2;
        }

        cv::Point2d center(const Detection &d)
        {
            cv::Point2d c{};
            for (auto p : d.corners)
                c += cv::Point2d(p);
            return c / 4;
        }

        bool validRaw(const Detection &d, cv::Size size, std::string &reason)
        {
            std::array<cv::Point2d, 4> points;
            for (size_t i = 0; i < 4; ++i)
                points[i] = d.corners[i];
            if (!validateStableCorners(points, d, size, 0, reason))
                return false;
            if (d.attributes.orientation)
            {
                std::array<bool, 4> used{};
                for (int s : *d.attributes.orientation)
                {
                    if (s < 0 || s >= 4 || used[s])
                    {
                        reason = "INVALID_ORIENTATION";
                        return false;
                    }
                    used[s] = true;
                }
            }
            auto ordered = orderScreenCycle(points, CornerConfig{}, reason);
            if (!ordered)
                return false;
            for (size_t i = 0; i < 4; ++i)
                if (ordered->screen_points[i] != points[i])
                {
                    reason = "INVALID_SCREEN_ORDER";
                    return false;
                }
            auto b = boundingBoxFromCorners(d.corners);
            if (d.bbox != b)
            {
                reason = "INVALID_RAW_BBOX";
                return false;
            }
            return true;
        }
    }

    TemporalResult TemporalStabilizer::update(const std::vector<Detection> &detections,
                                              const FrameStamp &stamp, cv::Size size)
    {
        TemporalResult result;
        std::string reason;
        // 所有候选一起防御，非法不能静默丢掉再声称唯一关联。
        auto invalid = [&](std::string r)
        {
            reset(ResetReason::InvalidSequence);
            result.status = Status::INVALID_INPUT;
            result.diagnostics.reset_or_fallback_reason = std::move(r);
            diagnostics_ = result.diagnostics;
            return result;
        };
        if (!validateFrameStamp(stamp, last_input_, reason))
            return invalid(reason);
        if (size.width <= 0 || size.height <= 0)
            return invalid("INVALID_SIZE");
        for (const auto &d : detections)
            if (!validRaw(d, size, reason))
                return invalid(reason);
        if (size_ && *size_ != size)
        {
            reset(ResetReason::InputChanged);
            result.diagnostics.reset_or_fallback_reason = "INPUT_CHANGED";
        }
        size_ = size;
        last_input_ = stamp;
        if (reference_ && double(stamp.timestamp_us - reference_->stamp.timestamp_us) / 1000.0 >
                              config_.history_max_gap_ms)
        {
            reference_.reset();
            smoothing_.reset();
            result.diagnostics.reset_or_fallback_reason = "HISTORY_EXPIRED";
        }
        if (detections.empty())
        {
            smoothing_.reset();
            result.status = Status::NOT_DETECTED;
            result.diagnostics.association.reason = "EMPTY_CURRENT";
            diagnostics_ = result.diagnostics;
            return result;
        }
        auto &diag = result.diagnostics;
        size_t chosen = 0;
        for (size_t i = 1; i < detections.size(); ++i)
            if (area(detections[i]) > area(detections[chosen]))
                chosen = i;
        diag.association.reason = "NO_HISTORY";
        if (reference_)
        {
            size_t matches = 0, match = 0;
            const double diagonal =
                std::hypot(double(reference_->raw.bbox.width), double(reference_->raw.bbox.height));
            for (size_t i = 0; i < detections.size(); ++i)
            {
                double ratio = area(detections[i]) / area(reference_->raw);
                if (cv::norm(center(detections[i]) - center(reference_->raw)) <=
                        config_.max_center_distance_diagonal_ratio * diagonal &&
                    ratio >= config_.min_area_ratio && ratio <= config_.max_area_ratio)
                {
                    ++matches;
                    match = i;
                }
            }
            if (matches == 1)
            {
                chosen = match;
                diag.association.matched_history = true;
                diag.association.reason = "MATCHED_HISTORY";
            }
            else
            {
                diag.association.ambiguous = matches > 1;
                diag.association.reason = matches ? "ASSOCIATION_AMBIGUOUS" : "ASSOCIATION_FAILED";
                smoothing_.reset();
            }
        }
        diag.association.current_index = chosen;
        const auto &current = detections[chosen];
        Detection output = current;
        std::array<cv::Point2d, 4> stable;
        for (size_t i = 0; i < 4; ++i)
            stable[i] = current.corners[i];
        if (config_.stabilization_enabled && smoothing_ && diag.association.matched_history)
        {
            const double dt = double(stamp.timestamp_us - smoothing_->stamp.timestamp_us) / 1e6;
            diag.dt_seconds = dt;
            if (dt == 0)
                reason = "ZERO_DT";
            else
            {
                diag.correspondence =
                    resolveCornerCorrespondence(current, smoothing_->raw, config_);
                if (!diag.correspondence.valid)
                    reason = diag.correspondence.reason;
                else
                {
                    double alpha = computeCurrentWeight(dt, tau_);
                    for (size_t i = 0; i < 4; ++i)
                        stable[i] =
                            alpha * cv::Point2d(current.corners[i]) +
                            (1 - alpha) *
                                smoothing_->stable[diag.correspondence.current_to_previous[i]];
                    if (validateStableCorners(stable, current, size,
                                              *config_.max_smoothing_deviation_px, reason))
                    {
                        // 转float后再验证当前slot，随后屏幕排序仅影响对外输出，不改变内部slot。
                        auto published = stable;
                        for (auto &p : published)
                            p = cv::Point2f(p);
                        if (validateStableCorners(published, current, size,
                                                  *config_.max_smoothing_deviation_px, reason))
                        {
                            auto order = orderScreenCycle(published, CornerConfig{}, reason);
                            if (order)
                            {
                                for (size_t i = 0; i < 4; ++i)
                                    output.corners[i] = order->screen_points[i];
                                output.bbox = boundingBoxFromCorners(output.corners);
                                if (current.attributes.orientation)
                                    for (size_t p = 0; p < 4; ++p)
                                        (*output.attributes.orientation)[p] =
                                            order->input_to_screen[(
                                                *current.attributes.orientation)[p]];
                                diag.output_slot_mapping = order->input_to_screen;
                                diag.used_smoothing = true;
                                diag.alpha = alpha;
                                reason.clear();
                            }
                        }
                    }
                }
            }
            if (!diag.used_smoothing)
            {
                diag.fell_back = true;
                diag.reset_or_fallback_reason = reason;
                for (size_t i = 0; i < 4; ++i)
                    stable[i] = current.corners[i];
                output = current;
            }
        }
        else if (diag.reset_or_fallback_reason.empty())
        {
            diag.reset_or_fallback_reason =
                !config_.stabilization_enabled
                    ? "SMOOTHING_DISABLED"
                    : (!diag.association.matched_history ? diag.association.reason
                                                         : "SMOOTHING_HISTORY_EMPTY");
            diag.fell_back = config_.stabilization_enabled;
        }
        reference_ = Reference{current, stamp};
        smoothing_ = Smoothing{current, stable, stamp};
        result.status = Status::DETECTED;
        result.tracks.push_back({chosen, output});
        diagnostics_ = diag;
        return result;
    }
}
