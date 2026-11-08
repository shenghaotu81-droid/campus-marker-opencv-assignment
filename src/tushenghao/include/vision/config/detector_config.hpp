#pragma once
#include <array>
#include <filesystem>

namespace mark::detector
{
    struct PreprocessConfig
    {
        int work_width_ = 960, work_height_ = 720, threshold_ = 200;
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

    // 检测参数由分割、几何匹配和角点解码共同使用。
    struct DetectorConfig
    {
        PreprocessConfig preprocess_;
        GeometryConfig geometry_;
        CornerConfig corner_;
    };
    DetectorConfig load_detector_config(const std::filesystem::path &);
}
