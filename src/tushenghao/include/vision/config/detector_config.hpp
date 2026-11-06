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
        double approximation_epsilon_ = 2.;
    };
    struct CornerConfig
    {
        std::array<int, 4> physical_to_screen_{{0, 1, 2, 3}};
    };
    struct DetectorConfig
    {
        PreprocessConfig preprocess;
        GeometryConfig geometry_;
        CornerConfig corner_;
    };
}
