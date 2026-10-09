// 原App无参静默退出0且只支持check-config；严格解析离线两模式，实际算法由公共process完成。
#include "mark/detector.hpp"
#include "offline_runner.hpp"
#include "run_console.hpp"
#include <iostream>
#include <limits>
#include <map>

namespace
{
    // 原主函数同时解析和运行；参数语法独立于打开输入/输出，输出开关保持无值且严格去重。
    struct CliOptions
    {
        std::map<std::string, std::string> values;
        bool check{false}, display{false}, export_video{false};
    };

    CliOptions parse_cli(int argc, char **argv)
    {
        CliOptions options;
        std::map<std::string, std::string> args;

        for (int i = 1; i < argc; ++i)
        {
            std::string key = argv[i];
            // 原 CLI 输出选项无实现；无值开关严格去重，不消费下一参数。
            if (key == "--display" || key == "--export-video")
            {
                auto &flag = key == "--display" ? options.display : options.export_video;
                if (flag)
                    throw std::runtime_error("duplicate option: " + key);
                flag = true;
                continue;
            }
            if (key == "--check-config")
            {
                if (options.check)
                    throw std::runtime_error("duplicate check-config");
                options.check = true;
                continue;
            }
            if (key != "--video" && key != "--config" && key != "--run-dir" && key != "--mode" &&
                key != "--run-purpose" && key != "--expected-frames")
                throw std::runtime_error("unknown option: " + key);
            if (i + 1 == argc || std::string(argv[i + 1]).rfind("--", 0) == 0 ||
                !args.emplace(key, argv[++i]).second)
                throw std::runtime_error("missing value or duplicate option");
        }
        options.values = std::move(args);
        return options;
    }
} // namespace

int main(int argc, char **argv)
{
    try
    {
        if (argc == 1)
        {
            std::cerr
                << "用法: marker_app --check-config [--config YAML] | --video VIDEO --config YAML --run-dir "
                   "NEW_DIRECTORY --mode baseline|debug [--run-purpose production|verification] [--display] "
                   "[--export-video] [--expected-frames N]\n";
            return 1;
        }
        auto options = parse_cli(argc, argv);
        auto &args = options.values;
        const bool check = options.check, display = options.display,
                   export_video = options.export_video;
        std::string config = args.count("--config")
                                 ? args["--config"]
                                 : (std::filesystem::path(__FILE__).parent_path().parent_path() /
                                    "config/detector.yaml")
                                       .string();
        auto app = mark::loadConfig(config);
        if (check)
        {
            if (display || export_video || args.size() > (args.count("--config") ? 1u : 0u))
                throw std::runtime_error("check-config conflicts with run options");
            mark::Detector d(app.detector_config);
            std::cout << "Config check passed\n";
            return 0;
        }
        if (!args.count("--video"))
            throw std::runtime_error("--video required");
        if (args.count("--run-dir"))
            app.offline.directory = args["--run-dir"];
        if (args.count("--mode"))
            app.offline.mode = args["--mode"];
        if ((display || export_video) && app.offline.mode != "debug")
            throw std::runtime_error("display/export-video requires debug mode");
        if (display)
            app.detector_config.output.show_window = true;
        if (export_video)
            app.offline.export_video = true;
        // 旧 CLI 没有独立期待帧数；严格十进制正整数只覆盖本次 App 完整性核验。
        if (args.count("--expected-frames"))
        {
            const auto &token = args.at("--expected-frames");
            if (token.empty() || token.find_first_not_of("0123456789") != std::string::npos)
                throw std::runtime_error("expected-frames requires positive integer");
            auto count = std::stoull(token);
            if (!count)
                throw std::runtime_error("expected-frames out of range");
            app.offline.expected_frame_count = count;
        }
        auto purpose = args.count("--run-purpose") ? args["--run-purpose"] : "production";
        if (purpose != "production" && purpose != "verification")
            throw std::runtime_error("invalid run-purpose");
        if (purpose == "verification" && app.offline.mode != "debug")
            throw std::runtime_error("verification requires debug mode");
        return mark::runOffline(app, args["--video"], config, mark::ExecutionScope::Full, {},
                                purpose);
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        mark::write_failure_console(std::cerr, e.what());
        return 1;
    }
}
