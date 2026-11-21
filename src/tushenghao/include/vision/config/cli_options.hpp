// 命令行入口解析器，只从终端读 --video <路径>，其余检查拒绝报错
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
        // 循环从 i = 1 开始，因为 argv[0] 是程序名本身
        for (int i = 1; i < argc; ++i)
        {
            const std::string key = argv[i];
            // 未知选项报错
            if (key != "--video")
                throw std::runtime_error("unknown option: " + key);
            // 缺值报错（C++ 保证 argv[argc] 是空指针；判断“下一个参数是不是以 -- 开头”（选项名））
            if (i + 1 == argc || std::string(argv[i + 1]).rfind("--", 0) == 0)
                throw std::runtime_error("missing value: " + key);
            // 重复选项报错（++i）
            if (!result.emplace(key, argv[++i]).second)
                throw std::runtime_error("duplicate option: " + key);
        }
        return result;
    }
}
