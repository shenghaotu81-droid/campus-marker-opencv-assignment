// 定义配置模块对外提供哪些功能(怎么加载（读取）/检查/保存配置)
#pragma once

#include <filesystem>

#include "application/app_config.hpp"

namespace mark
{
    // 从配置文件读取配置，生成 AppConfig(文件路径)  得到配置  auto可以自己推导类型
    AppConfig loadConfig(const std::filesystem::path &path);

    // Q03：模块独立输入校验；总入口按原顺序组合。
    void validateTemporalConfig(const TemporalConfig &);
    void validateDetectorConfig(const DetectorConfig &);
    void validateObservabilityConfig(const AppConfig &);

    // 检查配置是否合法
    void validateConfig(const AppConfig &config);

    // 把最终生效的配置写出去（要写出的配置，写到哪里）
    void writeEffectiveConfig(
        const AppConfig &config,
        const std::filesystem::path &path);

} // namespace mark