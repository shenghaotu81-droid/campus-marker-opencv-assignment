// 配置错误类型定义(当读取配置（yaml）失败时，不直接抛普通错误，而是抛一个专门表示“配置错误”的异常。)
#pragma once

#include <stdexcept>
#include <string>

namespace mark
{

    class ConfigError : public std::runtime_error // C++ 标准运行时错误(本质还是一个运行时错误，但是名字更具体，表示是配置错误)
    {
    public:
        explicit ConfigError(const std::string &message)
            : std::runtime_error(message)
        {
        }

        // TODO:
        // 当前文件路径与字段路径信息通过 message 字符串携带。
        // Current file path and field path information are carried by the message string.
        //
        // 等 config.cpp 实现具体抛错逻辑时，
        // 评估是否需要增加结构化字段（file_path / field_path）成员。
        //
        // When config.cpp implements error throwing logic,
        // evaluate whether structured members
        // (file_path / field_path) are required.
    };

} // namespace mark

/* 简化理解(runtime_error父类):throw,catch,初始化父类 runtime_error(初始化列表可以直接调用父类构造函数)
class runtime_error
{
private:
    string message_;

public:

    runtime_error(string msg)
    {
        message_ = msg;
    }

    const char* what();
};
*/