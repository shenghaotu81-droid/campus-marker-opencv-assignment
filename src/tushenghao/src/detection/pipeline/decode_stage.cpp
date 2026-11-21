// 把匹配得到的粗四边形转成原图上的检测角点。
#include "pipeline/decode_stage.hpp"
#include "preprocess/preprocess.hpp"
#include "geometry/geometry_observation.hpp"
#include "corners/corner_edge_fit.hpp"
#include <set>
#include "corners/corner_observation.hpp"

namespace mark::detector
{
    // 按物理顺序逐角精修，四角必须来自不同的原图轮廓。
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

    std::optional<Detection> publish_float_detection(const CornerPoints &points, cv::Size size, const CornerConfig &config, std::string &reason_)
    {
        // 先把角点舍入到 float，再升回 double 检查实际要发布的值。
        CornerPoints rounded;
        for (size_t i = 0; i < 4; ++i)

            rounded[i] = cv::Point2f(points[i]);

        // 在舍入后的输入轮廓上复核凸性，再转成屏幕顺序。
        auto ordered = order_screen_corners(rounded, reason_);
        if (!ordered)
            return {};

        Detection d;
        // 下游通过 orientation_ 从屏幕顺序恢复物理角顺序。
        d.orientation_ = config.physical_to_screen_;

        for (size_t i = 0; i < 4; ++i)
        {
            auto p = (*ordered)[i];
            if (p.x < 0 || p.y < 0 || p.x >= size.width || p.y >= size.height)
            {
                reason_ = "CORNER_OUT_OF_IMAGE";
                return {};
            }
            // 写回已按 float 舍入并检查过的坐标。
            d.corners_[i] = cv::Point2f(p);
        }

        return d;
    }

    FrameResult decode_stage(const PreparedFrame &frame, const std::vector<MatchQuad> &matches, const CornerConfig &config)
    {
        FrameResult result;
        result.status_ = Status::NOT_DETECTED;

        // 同一帧的所有匹配共用原图轮廓索引。
        OriginalContourIndex index(frame);

        // 逐个匹配尝试。失败时记录原因，再继续下一个匹配。
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
