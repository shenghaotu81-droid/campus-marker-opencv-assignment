#pragma once
#include <string>
#include "vision/config/detector_config.hpp"

namespace mark
{
    // 离线输出独立于检测参数；关闭叠加后仍写位姿、轨迹和汇总。
    struct OfflineRunConfig
    {
        std::string run_directory_{"outputs/marker"};
        std::string video_filename_{"overlay.avi"};
        bool overlay_{true};
    };

    struct AppConfig
    {
        detector::DetectorConfig detector_config_;
        OfflineRunConfig offline_;
    };

    OfflineRunConfig load_offline_config(const std::filesystem::path &);
}
