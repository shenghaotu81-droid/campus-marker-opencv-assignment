// 薄壳（入口），当前真正的管线主干是 offline_runner.cpp（检测、位姿、跟踪、导出、渲染）
#include "vision/config/cli_options.hpp"
#include "vision/config/app_config.hpp"
#include "offline/offline_runner.hpp"
#include <iostream>
#include <filesystem>
#include <exception>

int main(int argc, char **argv)
{
    try
    {
        // 固定配置目录，分别加载检测参数和离线输出选项，再启动完整视频管线。
        const auto args = mark::parse_options(argc, argv);  // “给出来的参数格式对不对”（格式）
        // 检查“map 里没有 --video”（必填项）
        if (!args.count("--video"))
            throw std::runtime_error("用法: marker_app --video VIDEO");  // “必填的参数到底给没给”（有无）
        // 拼出配置目录（当前VISION_SOURCE_ROOT 是编译时由 CMake 注入的源码根目录宏:顶层set(VISION_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")，该层target_compile_definitions(marker_app PRIVATE VISION_SOURCE_ROOT="${VISION_ROOT}")）
        const auto directory = std::filesystem::path(VISION_SOURCE_ROOT) / "config";
        // 读取装入两个配置（读检测参数；读离线输出项）；用大括号聚合初始化；pose.yaml 和 tracking.yaml由run_offline()内部加载（分层读取配置）
        mark::AppConfig app{mark::detector::load_detector_config(directory / "detector.yaml"),
                            mark::load_offline_config(directory / "offline.yaml")};
        // 跑完整管线；return 把管线的退出码（0 成功）返回给操作系统
        return mark::run_offline(app, args.at("--video"), directory);
    }
    catch (const std::exception &e)
    {
        std::cerr << "marker_app: " << e.what() << '\n';
        return 1;
    }
}
