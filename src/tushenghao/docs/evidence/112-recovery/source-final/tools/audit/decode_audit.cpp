// 保留旧位置参数和显式帧子集，改用同一runner/serializer；未调process不能报告公共耗时。
#include "offline_runner.hpp"
#include <iostream>
#include <charconv>
#include <sstream>

int main(int argc, char **argv)
{
    try
    {
        std::vector<std::string> positional;
        std::string directory;
        for (int i = 1; i < argc; ++i)
        {
            std::string s = argv[i];
            if (s == "--run-dir")
            {
                if (!directory.empty() || i + 1 == argc)
                    throw std::runtime_error("run-dir duplicate/missing");
                directory = argv[++i];
            }
            else if (s.rfind("--", 0) == 0)
                throw std::runtime_error("unknown option");
            else
                positional.push_back(s);
        }
        if (positional.empty() || positional.size() > 3)
            throw std::runtime_error(
                "decode_audit VIDEO [all|frame,list] [CONFIG] --run-dir NEW_DIRECTORY");
        std::set<uint64_t> subset;
        if (positional.size() > 1 && positional[1] != "all")
        {
            std::istringstream input(positional[1]);
            std::string token;
            while (std::getline(input, token, ','))
            {
                uint64_t n;
                auto p = std::from_chars(token.data(), token.data() + token.size(), n);
                if (p.ec != std::errc{} || p.ptr != token.data() + token.size() ||
                    !subset.insert(n).second)
                    throw std::runtime_error("invalid/duplicate frame list");
            }
        }
        std::string config = positional.size() == 3 ? positional[2] : MARK_DEFAULT_CONFIG;
        auto app = mark::loadConfig(config);
        app.offline.directory = directory.empty() ? "decode-audit.run" : directory;
        app.offline.mode = "debug";
        if (app.diagnostics.level == "summary")
            app.diagnostics.level = "frame";
        return mark::runOffline(app, positional[0], config, mark::ExecutionScope::Decode, subset);
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
