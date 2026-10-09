// 标定工具独立统计支持像素；不改变生产拟合的访问序列及权重。
#pragma once

#include <cmath>
#include <cstddef>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#include <opencv2/core.hpp>

namespace block3_fixture_tools
{
    // 旧计数将轮廓回走次数当成独立支持点；按坐标精确去重，与生产门槛一致。
    // 非有限坐标是测量输入错误，不能进入排序集合或被静默忽略。
    inline std::size_t count_unique_support_pixels(
        const std::vector<cv::Point2d>& support_arc)
    {
        std::set<std::pair<double, double>> pixels;
        for (const auto& point : support_arc)
        {
            if (!std::isfinite(point.x) || !std::isfinite(point.y))
            {
                throw std::invalid_argument("SUPPORT_PIXEL_COORDINATE_NOT_FINITE");
            }
            pixels.emplace(point.x, point.y);
        }
        return pixels.size();
    }
}
