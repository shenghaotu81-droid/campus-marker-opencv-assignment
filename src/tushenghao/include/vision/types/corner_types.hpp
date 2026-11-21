#pragma once
#include <array>
#include <optional>
#include <string>
#include <opencv2/core.hpp>

namespace mark::detector
{
    // 四个像素坐标，顺序由使用它的接口约定。
    using CornerPoints = std::array<cv::Point2d, 4>;

    // 精修结果：成功给 4 个角点，失败给原因
    struct CornerResolution
    {
        std::optional<CornerPoints> corners_;
        std::string reason_;
    };

    // 输入凸环；输出LT/RT/RB/LB。物理身份由调用方查安装配置，不由排序器推断。
    std::optional<CornerPoints> order_screen_corners(const CornerPoints &, std::string &reason_);
}
