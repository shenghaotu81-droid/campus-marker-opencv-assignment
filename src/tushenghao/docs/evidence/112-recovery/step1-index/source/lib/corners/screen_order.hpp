// 几何环slot没有物理P编号；复用原排序核心，不将未知方向补成物理身份。
#pragma once
#include "mark/detector_config.hpp"
#include <opencv2/core.hpp>
#include <array>
#include <optional>
#include <string>
namespace mark {
struct ScreenCycleOrder {
    std::array<cv::Point2d,4> screen_points;
    std::array<int,4> input_to_screen;
    bool tie{false};
};
std::optional<ScreenCycleOrder> orderScreenCycle(const std::array<cv::Point2d,4>&,
                                               const CornerConfig&, std::string& reason);
}
