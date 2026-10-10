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

    // 每条重叠弧原来都分配坐标/相位向量；调用方持有局部 scratch，清空后复用容量。
    struct PeriodicLineFitScratch
    {
        struct ScanPoint
        {
            double t, z, h;
        };

        std::vector<ScanPoint> points;
        std::array<std::vector<double>, 2> groups;
    };

    std::optional<PeriodicLineFit> fitPeriodicLine(const std::vector<cv::Point2d> &support,
                                                   LineFitModel model);
    std::optional<PeriodicLineFit> fitPeriodicLine(const std::vector<cv::Point2d> &support,
                                                   LineFitModel model,
                                                   PeriodicLineFitScratch &scratch);
    double scanPhase(cv::Point2d point, int axis);
    double periodicLineDistance(cv::Point2d point, const cv::Vec4d &line, int axis,
                                double coefficient);
}
