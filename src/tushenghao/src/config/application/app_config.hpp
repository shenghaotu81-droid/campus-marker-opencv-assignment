#pragma once
#include "vision/config/detector_config.hpp"
namespace mark {
struct OfflineRunConfig {
    std::string directory{"outputs/marker"};
    std::string video_fourcc{"MJPG"}, video_filename{"overlay.mp4"};
};
struct AppConfig {
    DetectorConfig detector_config;
    OfflineRunConfig offline;
};
} // namespace mark
