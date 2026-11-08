#pragma once
#include <map>
#include <stdexcept>
#include <string>

namespace mark
{
    // 终端输入只接受 --video 的一个值，逐项报告未知、缺值和重复参数。
    inline std::map<std::string, std::string> parse_options(int argc, char **argv)
    {
        std::map<std::string, std::string> result;
        for (int i = 1; i < argc; ++i)
        {
            const std::string key = argv[i];
            if (key != "--video") throw std::runtime_error("unknown option: " + key);
            if (i + 1 == argc || std::string(argv[i + 1]).rfind("--", 0) == 0)
                throw std::runtime_error("missing value: " + key);
            if (!result.emplace(key, argv[++i]).second)
                throw std::runtime_error("duplicate option: " + key);
        }
        return result;
    }
}
