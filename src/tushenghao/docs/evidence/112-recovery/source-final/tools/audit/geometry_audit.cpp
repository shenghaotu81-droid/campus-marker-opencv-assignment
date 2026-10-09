// 新阶段工具复用完整几何编排和统一记录；有假设不等于DETECTED，不添加准入判据。
#include "offline_runner.hpp"
#include <iostream>
#include <map>

int main(int argc, char **argv)
{
    try
    {
        std::map<std::string, std::string> args;
        for (int i = 1; i < argc; ++i)
        {
            std::string key = argv[i];
            if (key != "--video" && key != "--config" && key != "--run-dir")
                throw std::runtime_error("unknown option");
            if (i + 1 == argc || !args.emplace(key, argv[++i]).second)
                throw std::runtime_error("missing/duplicate option");
        }
        for (auto key : {"--video", "--config", "--run-dir"})
            if (!args.count(key))
                throw std::runtime_error("required option missing");
        auto app = mark::loadConfig(args["--config"]);
        app.offline.directory = args["--run-dir"];
        app.offline.mode = "debug";
        if (app.diagnostics.level == "summary")
            app.diagnostics.level = "frame";
        return mark::runOffline(app, args["--video"], args["--config"],
                                mark::ExecutionScope::Geometry);
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
