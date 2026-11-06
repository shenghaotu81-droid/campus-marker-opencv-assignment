#pragma once
#include "vision/types/detection/corner_types.hpp"
#include <vector>
namespace mark
{
// 单一真实轮廓，两条四边形邻边指导；每边恰一次L2，不枚举/评分/回退。
    std::optional<cv::Point2d> fitObservedCorner(const std::vector<cv::Point> &, cv::Point2d,
                                                 const std::array<cv::Point2d, 2> &adjacent, std::string &reason);
}
