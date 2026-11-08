#pragma once
#include <array>

namespace mark
{
    struct PreprocessConfig
    {
        int work_width = 960, work_height = 720, threshold = 200;
    };

    struct GeometryConfig
    {
        int white_threshold_ = 200;
        // cv::approxPolyDP 的拟合误差
        double approximation_epsilon_ = 2.0;
    };

    struct CornerConfig
    {
        std::array<int, 4> physical_to_screen_{{0, 1, 2, 3}};
    };

    // 库通用配置接口
    struct DetectorConfig
    {
        PreprocessConfig preprocess;
        GeometryConfig geometry_;
        CornerConfig corner_;
    };
}
