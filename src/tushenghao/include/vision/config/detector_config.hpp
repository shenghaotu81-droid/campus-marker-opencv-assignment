#pragma once
#include <array>
#include <filesystem>

namespace mark::detector
{
    struct PreprocessConfig
    {
        int work_width_ = 960, work_height_ = 720, threshold_ = 200; // 原图精测：先被存进 PreparedFrame 叫 original_white_threshold_，然后在 corner_observation.cpp 里，二值化原图（全分辨率）；角点精拟合时，用于在原图上找每个角附近的真实轮廓——精测
    };

    struct GeometryConfig
    {
        int white_threshold_ = 200; // 小图粗找：二值化工作图（在 geometry_observation.cpp 里，extract_white_components 用它把工作图切成黑白，找白色块——粗找候选）
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
