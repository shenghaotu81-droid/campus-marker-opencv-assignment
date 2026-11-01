#pragma once
#include <stdexcept>
#include <string>
namespace mark {
// Q01/Q03：配置读取与参数核验失败，错误正文保留文件、字段路径及原因。
class ConfigError : public std::runtime_error {
public:
    explicit ConfigError(const std::string &message) : std::runtime_error(message) {}
};
}
