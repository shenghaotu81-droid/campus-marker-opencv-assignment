// 把工作图四边形映射到原图角片，逐角拟合并发布可供位姿使用的浮点四角。
#include "pipeline/decode_stage.hpp"
#include "preprocess/preprocess.hpp"
#include "geometry/geometry_observation.hpp"
#include "corners/corner_edge_fit.hpp"
#include <set>
#include "corners/corner_observation.hpp"

namespace mark::detector
{
    // 原图轮廓逐角拟合，确保四个物理角使用不同组件。
    CornerResolution resolve_observed_corners(const CornerPoints &physical, OriginalContourIndex &index)
    {
        CornerPoints measured;
        std::set<size_t> used;
        for (size_t i = 0; i < 4; ++i)
        {
            auto source = index.at_corner(physical[i]);
            if (!source.contour_)
                return {{}, source.reason_};
            if (!used.insert(source.component_id_).second)
                return {{}, "REUSED_CORNER_COMPONENT"};
            std::string reason_;
            auto corner = fit_observed_corner(*source.contour_, physical[i], {physical[(i + 3) % 4], physical[(i + 1) % 4]}, reason_);
            if (!corner)
                return {{}, reason_};
            measured[i] = *corner;
        }
        return {measured, {}};
    }

    // float 舍入后复核凸环和图像边界，再向位姿模块发布屏幕角。
    std::optional<Detection> publish_float_detection(const CornerPoints &points, cv::Size size, const CornerConfig &config, std::string &reason_)
    {
        CornerPoints rounded;
        for (size_t i = 0; i < 4; ++i)
            rounded[i] = cv::Point2f(points[i]);
        auto ordered = order_screen_corners(rounded, reason_);
        if (!ordered)
            return {};
        Detection d;
        d.orientation_ = config.physical_to_screen_;
        for (size_t i = 0; i < 4; ++i)
        {
            auto p = (*ordered)[i];
            if (p.x < 0 || p.y < 0 || p.x >= size.width || p.y >= size.height)
            {
                reason_ = "CORNER_OUT_OF_IMAGE";
                return {};
            }
            d.corners_[i] = cv::Point2f(p);
        }
        return d;
    }

    FrameResult decode_stage(const PreparedFrame &frame, const std::vector<MatchQuad> &matches, const CornerConfig &config)
    {
        FrameResult result;
        result.status_ = Status::NOT_DETECTED;
        OriginalContourIndex index(frame);
        for (const auto &match:matches)
        {
            std::string reason_;
            auto screen = order_screen_corners(match, reason_);
            if (!screen)
            {
                result.diagnostics_.push_back(reason_);
                continue;
            }
            CornerPoints physical;
            for (size_t p = 0; p < 4; ++p)
                physical[p] = (*screen)[config.physical_to_screen_[p]];
            auto measured = resolve_observed_corners(physical, index);
            if (!measured.corners_)
            {
                result.diagnostics_.push_back(measured.reason_);
                continue;
            }
            auto d = publish_float_detection(*measured.corners_, frame.original_image_.size(), config, reason_);
            if (d)
                result.detections_.push_back(*d);
            else
                result.diagnostics_.push_back(reason_);
        }
        if (!result.detections_.empty())
            result.status_ = Status::DETECTED;
        return result;
    }

    FrameResult run_decode_pipeline(const FrameInput &input, const DetectorConfig &config)
    {
        auto frame = preprocess(input, config.preprocess_);
        auto components = extract_white_components(frame, config.geometry_);
        return decode_stage(frame, match_quads(frame, components, config.geometry_), config.corner_);
    }

}
