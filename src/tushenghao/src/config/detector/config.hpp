#pragma once
#include <filesystem>
#include "application/app_config.hpp"

namespace mark
{
    AppConfig loadConfig(const std::filesystem::path &);
    void validateDetectorConfig(const DetectorConfig &);
    void validateOfflineConfig(const AppConfig &);
}
