#pragma once
#include <string>
#include "vision/config/detector_config.hpp"

namespace mark
{
    struct OfflineRunConfig
    {
        std::string directory{"outputs/marker"};
        std::string video_fourcc{"MJPG"}, video_filename{"overlay.avi"};
    };

    // 应用层拼接的配置结构体
    struct AppConfig
    {
        DetectorConfig detector_config;
        OfflineRunConfig offline;
    };
} // namespace mark
