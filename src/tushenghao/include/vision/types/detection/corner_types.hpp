#pragma once
#include <array>
#include <optional>
#include <string>
#include <opencv2/core.hpp>

namespace mark
{
    using CornerPoints = std::array<cv::Point2d, 4>;

    struct CornerResolution
    {
        std::optional<CornerPoints> corners;
        std::string reason;
    };

    // 输入凸环；输出LT/RT/RB/LB。物理身份由调用方查安装配置，不由排序器推断。
    std::optional<CornerPoints> orderScreenCorners(const CornerPoints &, std::string &reason);
}
