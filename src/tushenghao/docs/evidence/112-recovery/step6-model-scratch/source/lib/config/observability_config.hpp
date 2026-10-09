#pragma once
#include <cstdint>
#include <optional>
#include <string>
namespace mark {
// 应用观测值与算法配置分开；旧timing/candidate/window成员作为唯一生效开关，不另设冲突默认。
struct DiagnosticsConfig {
    std::string level{"summary"};
    uint64_t detail_first{0};
    std::optional<uint64_t> detail_last;
    uint64_t detail_interval{1};
};
struct RenderConfig {
    bool draw_raw{true}, draw_stable{true}, draw_candidates{false}, draw_corner_evidence{false},
        draw_timing{false}, show_held_state{false};
};
struct OfflineRunConfig {
    std::string mode{"baseline"}, directory;
    bool export_evidence{false}, export_video{false};
    double playback_fps{0};
    // 原来无法提供独立完整性证据；0 表示仅尝试使用容器计数。
    uint64_t expected_frame_count{0};
    // 原导出/显示无可核工程配置；仅约束 App 编码与探测，不进入算法预算。
    std::string video_fourcc{"MJPG"}, video_filename{"overlay.mp4"};
    int display_probe_timeout_ms{3000};
};
} // namespace mark
