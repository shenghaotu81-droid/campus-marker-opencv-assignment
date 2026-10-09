// 整个应用程序的配置（包含所有应用的配置）配置长什么样
#pragma once

#include "mark/detector_config.hpp"
#include "config/observability_config.hpp"

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
        RenderConfig render;
        OfflineRunConfig offline;

        // TODO:
        // Application-level configuration fields are added when
        // 当应用层配置的接口契约（contract）冻结后，
        // their contracts are frozen.
        // 再添加对应的应用级配置字段。
        //
        // Do not put FileNode/parser objects here.
        // 不要在这里放 FileNode / 配置解析器对象。
        // Do not put detector-unrelated video/window handling into DetectorConfig.
        // 不要把与 Detector 无关的视频处理、窗口显示逻辑放入 DetectorConfig。
    };

} // namespace mark
