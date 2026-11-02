// 整个应用程序的配置（包含所有应用的配置）配置长什么样
#pragma once

#include "vision/config/detector_config.hpp"
#include "application/observability_config.hpp"

namespace mark
{

    struct AppConfig
    {
        // Detector configuration.
        // 检测器配置。
        // Detector-specific fields are defined by DetectorConfig.
        // 与 Detector 相关的具体字段由 DetectorConfig 定义。
        DetectorConfig detector_config;
        DiagnosticsConfig diagnostics;
        // A03：窗口属于应用生命周期，不进入算法配置。
        bool show_window{false};
        RenderConfig render;
        OfflineRunConfig offline;

    };

} // namespace mark
