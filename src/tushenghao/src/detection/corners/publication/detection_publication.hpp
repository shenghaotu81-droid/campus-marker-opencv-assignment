#pragma once
#include "vision/types/detection/corner_types.hpp"
#include "vision/types/detector_types.hpp"
#include "vision/config/detector_config.hpp"
namespace mark
{
    std::optional<Detection> publishFloatDetection(const CornerPoints &, cv::Size, const CornerConfig &, std::string &);
}
