#pragma once
#include "vision/config/app_config.hpp"

namespace mark
{
    int run_offline(const AppConfig &, const std::string &video,
                    const std::filesystem::path &config_directory);
}
