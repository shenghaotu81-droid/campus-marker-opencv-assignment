// 三历史各自所有，不给Detection增加时序身份或伪置信度。
#pragma once
#include "core/frame_stamp.hpp"
namespace mark {
struct AssociationResult {
    std::optional<size_t> current_index;
    bool matched_history{false}, ambiguous{false};
    std::string reason;
};
struct CornerCorrespondence {
    std::array<size_t,4> current_to_previous{};
    bool valid{false};
    std::string reason;
    // 四个循环的保守误差区间；物理对应不需要这些数值。
    std::optional<std::array<double,4>> energy, lower, upper;
};
struct SemanticDisplaySample {
    std::string value;
    uint64_t source_frame_id{};
    int64_t source_timestamp_us{};
};
struct TemporalDiagnostics {
    std::optional<double> dt_seconds, alpha;
    AssociationResult association;
    CornerCorrespondence correspondence;
    bool used_smoothing{false}, fell_back{false};
    std::string reset_or_fallback_reason;
    std::optional<std::array<int,4>> output_slot_mapping;
};
struct TemporalResult {
    Status status{Status::NOT_READY};
    std::vector<TrackResult> tracks;
    TemporalDiagnostics diagnostics;
};
}
