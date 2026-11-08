#pragma once
#include "vision/types/detector_types.hpp"
#include "vision/types/prepared_frame_types.hpp"
#include "vision/types/corner_types.hpp"
#include "vision/config/detector_config.hpp"
#include "geometry/match_quad.hpp"

namespace mark::detector
{
    class OriginalContourIndex;
    // 原图角解析和 float 发布共用解码步骤的声明，数学边界可独立验证。
    CornerResolution resolve_observed_corners(const CornerPoints &, OriginalContourIndex &);
    std::optional<Detection> publish_float_detection(const CornerPoints &, cv::Size,
                                                    const CornerConfig &, std::string &);
    FrameResult decode_stage(const PreparedFrame &, const std::vector<MatchQuad> &, const CornerConfig &);
    FrameResult run_decode_pipeline(const FrameInput &, const DetectorConfig &);
}
