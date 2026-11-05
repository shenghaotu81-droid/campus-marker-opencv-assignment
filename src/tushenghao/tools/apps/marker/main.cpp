#include "vision/config/cli_options.hpp"
#include "detector/config.hpp"
#include "offline/offline_runner.hpp"
#include <iostream>
#include <limits>
int main(int argc, char **argv) {
    try {
        const auto args = mark::parse_options(argc, argv,
            {"--video", "--camera", "--config"}, {}, true,
            [](mark::OptionError e, const std::string &key) {
                throw std::runtime_error((e == mark::OptionError::Unknown ? "unknown option: " :
                    e == mark::OptionError::MissingValue ? "missing value: " : "duplicate option: ") + key);
            });
        if (args.count("--video") + args.count("--camera") != 1)
            throw std::runtime_error("用法: marker_app (--video VIDEO | --camera INDEX) [--config YAML]");
        const auto config = args.count("--config") ? args.at("--config") :
            (std::filesystem::path(VISION_SOURCE_ROOT) / "config/detector.yaml").string();
        std::string source;
        if (args.count("--camera")) {
            const auto index = mark::decimal_uint64(args.at("--camera"));
            if (!index || *index > std::numeric_limits<int>::max())
                throw std::runtime_error("camera requires nonnegative integer index");
            source = "camera:" + std::to_string(*index);
        } else source = args.at("--video");
        return mark::runOffline(mark::loadConfig(config), source, config);
    } catch (const std::exception &e) {
        std::cerr << "marker_app: " << e.what() << '\n';
        return 1;
    }
}
