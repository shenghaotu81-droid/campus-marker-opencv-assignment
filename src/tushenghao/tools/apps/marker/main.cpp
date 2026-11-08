#include "vision/config/cli_options.hpp"
#include "vision/config/app_config.hpp"
#include "offline/offline_runner.hpp"
#include <iostream>

int main(int argc, char **argv)
{
    try
    {
        // 固定配置目录，分别加载检测参数和离线输出选项，再启动完整视频管线。
        const auto args = mark::parse_options(argc, argv);
        if (!args.count("--video")) throw std::runtime_error("用法: marker_app --video VIDEO");
        const auto directory = std::filesystem::path(VISION_SOURCE_ROOT) / "config";
        mark::AppConfig app{mark::detector::load_detector_config(directory / "detector.yaml"),
                            mark::load_offline_config(directory / "offline.yaml")};
        return mark::run_offline(app, args.at("--video"), directory);
    }
    catch (const std::exception &e)
    {
        std::cerr << "marker_app: " << e.what() << '\n';
        return 1;
    }
}
