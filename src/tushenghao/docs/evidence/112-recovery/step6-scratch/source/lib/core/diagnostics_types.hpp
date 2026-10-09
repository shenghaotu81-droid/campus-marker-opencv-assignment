#pragma once
#include "mark/detector_types.hpp"
#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mark
{
    enum class Stage
    {
        Capture,
        Preprocess,
        Detect,
        Decode,
        Stabilize,
        ProcessTotal,
        Visualize,
        Wait,
        Export
    };
    enum class TimingStatus
    {
        MEASURED,
        DISABLED,
        SKIPPED,
        NOT_IMPLEMENTED,
        NOT_EXECUTED
    };
    enum class ExecutionScope
    {
        Full,
        Geometry,
        Decode,
        Temporal
    };
    enum class DiagnosticsLevel
    {
        Summary,
        Frame,
        Evidence
    };
    enum class ReasonCode
    {
        InputFormat,
        InvalidStamp,
        InvalidSequence,
        BudgetMissing,
        ClearlyIncomplete,
        AssignmentInsufficient,
        CornerRejected,
        ScreenRejected,
        ValidationRejected,
        UnresolvedCompetition,
        SemanticRejected,
        PublishFloatRejected,
        HistoryExpired,
        AssociationFailed,
        AssociationAmbiguous,
        CorrespondenceAmbiguous,
        ZeroDt,
        SmoothingRejected,
        ResetExternal,
        ResetInputChanged,
        ResetInvalidSequence,
        NonFiniteField,
        IoFailure,
        OtherRecordedReason
    };

    // 原日志把关闭与未执行写成同一数值；固定九槽并显式初始化，缺测不伪造零耗时。
    struct StageTiming
    {
        Stage stage;
        TimingStatus status{TimingStatus::NOT_EXECUTED};
        std::optional<std::chrono::nanoseconds> elapsed;
    };

    using TimingSnapshot = std::array<StageTiming, 9>;

    struct ReasonEvent
    {
        Stage stage;
        ReasonCode reason;
        std::string detail;
        std::optional<uint64_t> source_frame_id, hypothesis_id, component_id;
        uint64_t occurrences{1};
    };

    // reset原来直接清诊断；保留无分配、饱和计数，noexcept reset可安全记录调用原因。
    struct ResetSnapshot
    {
        std::array<uint64_t, 3> counts{};
        std::optional<ResetReason> last;
        bool saturated{false};
        void add(ResetReason reason) noexcept;

        bool empty() const noexcept
        {
            return !last.has_value();
        }
    };
    struct StageDetails;

    struct FrameCounts
    {
        std::optional<uint64_t> components, observations, generated, validated, completed,
            measurements, detections, tracks;
        bool truncated{false};
    };

    // 具体几何详情只前向声明；汇总模式不分配详情或复制公共结果，避免core反向依赖pipeline。
    struct FrameRecord
    {
        int record_schema_version{1};
        std::string run_id;
        uint64_t frame_id{};
        int64_t source_timestamp_us{};
        TimestampSource timestamp_source{TimestampSource::Unknown};
        std::string timestamp_recipe{"input"};
        ExecutionScope execution_scope{ExecutionScope::Full};
        cv::Size original_size{}, work_size{};
        std::optional<Status> result_status;
        std::optional<std::string> geometry_scope_result;
        TimingSnapshot timings{{{Stage::Capture},
                                {Stage::Preprocess},
                                {Stage::Detect},
                                {Stage::Decode},
                                {Stage::Stabilize},
                                {Stage::ProcessTotal},
                                {Stage::Visualize},
                                {Stage::Wait},
                                {Stage::Export}}};
        std::string process_total_boundary{"internal_scope"};
        std::vector<ReasonEvent> events;
        std::optional<ReasonCode> last_reset_reason;
        std::optional<uint64_t> last_reset_source_frame_id;
        FrameCounts counts;
        std::optional<FrameResult> output;
        std::shared_ptr<const StageDetails> details;
        std::optional<uint64_t> slot_overwrite_count, consumed_frame_count;
        std::optional<int64_t> enqueue_timestamp_ns;
        std::optional<double> enqueue_to_result_us;
        std::optional<std::string> clock_domain;
        std::string realtime_applicability{"not_applicable"};
        bool selected{false}, run_failed{false};
        std::optional<std::chrono::nanoseconds> diagnostics_construct_time;
    };

    const char *stageName(Stage);
    const char *timingStatusName(TimingStatus);
    const char *reasonName(ReasonCode);
    const char *scopeName(ExecutionScope);
}
