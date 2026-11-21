#pragma once
#include "vision/config/app_config.hpp"
#include <string>
#include <filesystem>

namespace mark
{
    int run_offline(const AppConfig &, const std::string &video,
                    const std::filesystem::path &config_directory);
}
