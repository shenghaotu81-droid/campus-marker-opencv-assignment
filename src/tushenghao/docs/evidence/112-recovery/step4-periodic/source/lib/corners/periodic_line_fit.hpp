// 单线不能解释扫描行奇偶偏移；本纯函数只消费完整有序支持，validator 可独立重建参数。
#pragma once
#include "corners/corner_types.hpp"
#include <optional>

namespace mark
{
    struct PeriodicLineFit
    {
        cv::Vec4d line{};
        int axis{1};
        double coefficient{0};
    };

    std::optional<PeriodicLineFit> fitPeriodicLine(const std::vector<cv::Point2d> &support,
                                                   LineFitModel model);
    double scanPhase(cv::Point2d point, int axis);
    double periodicLineDistance(cv::Point2d point, const cv::Vec4d &line, int axis,
                                double coefficient);
}
