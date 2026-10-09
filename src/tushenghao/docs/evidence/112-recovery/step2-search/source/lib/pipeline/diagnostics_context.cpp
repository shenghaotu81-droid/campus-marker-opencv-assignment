#include "pipeline/diagnostics_context.hpp"

namespace mark
{
    // 每次调用建立独立身份与状态；只在详细选中帧分配载荷，避免沿用上一帧证据。
    void FrameDiagnosticsContext::begin(const FrameInput &frame, const DiagnosticsRequest &r)
    {
        auto start = r.timing_enabled ? std::chrono::steady_clock::now()
                                      : std::chrono::steady_clock::time_point{};
        request = r;
        record = {};
        record.run_id = r.run_id;
        record.frame_id = frame.frame_id;
        record.source_timestamp_us = frame.timestamp_us;
        record.timestamp_source = frame.time_source;
        record.timestamp_recipe = r.timestamp_recipe;
        record.original_size = frame.image.size();
        record.execution_scope = r.scope;
        record.selected = r.selected && r.level != DiagnosticsLevel::Summary;
        timing.initialize(r.timing_enabled, r.scope);
        details.reset();
        if (record.selected)
        {
            details = std::make_shared<StageDetails>();
            record.details = details;
        }
        if (r.timing_enabled)
            record.diagnostics_construct_time = std::chrono::steady_clock::now() - start;
    }

    // 类型化事件直接保留实际分支原因；测构造区间，不能把日志成本藏到算法计时之外。
    void FrameDiagnosticsContext::event(Stage stage, ReasonCode reason, std::string detail,
                                        std::optional<uint64_t> hypothesis)
    {
        auto start = request.timing_enabled ? std::chrono::steady_clock::now()
                                            : std::chrono::steady_clock::time_point{};
        record.events.push_back(
            {stage, reason, std::move(detail), record.frame_id, hypothesis, std::nullopt, 1});
        if (request.timing_enabled)
            *record.diagnostics_construct_time += std::chrono::steady_clock::now() - start;
    }

    // 原reset原因被清空；按语义调用数输出，不把外部调用伪归因给下一输入。
    void FrameDiagnosticsContext::resets(const ResetSnapshot &reset, std::optional<uint64_t> source)
    {
        const ReasonCode codes[] = {ReasonCode::ResetExternal, ReasonCode::ResetInputChanged,
                                    ReasonCode::ResetInvalidSequence};
        if (reset.last)
        {
            record.last_reset_reason = codes[*reset.last == ResetReason::External       ? 0
                                             : *reset.last == ResetReason::InputChanged ? 1
                                                                                        : 2];
            record.last_reset_source_frame_id = source;
        }
        for (size_t i = 0; i < 3; ++i)
            if (reset.counts[i])
                record.events.push_back(
                    {Stage::Stabilize, codes[i],
                     reset.saturated ? "reset count saturated" : "reset invoked", source,
                     std::nullopt, std::nullopt, reset.counts[i]});
    }

    // 汇总保留真实状态计数；仅采样时复制公开结果，不用历史替代当前输出。
    void FrameDiagnosticsContext::output(const FrameResult &result)
    {
        auto start = request.timing_enabled ? std::chrono::steady_clock::now()
                                            : std::chrono::steady_clock::time_point{};
        record.result_status = result.status;
        if (result.status == Status::DETECTED || result.status == Status::NOT_DETECTED)
        {
            record.counts.detections = result.detections.size();
            if (request.scope == ExecutionScope::Full || request.scope == ExecutionScope::Temporal)
                record.counts.tracks = result.tracks.size();
        }
        if (record.selected)
            record.output = result;
        if (request.timing_enabled)
            *record.diagnostics_construct_time += std::chrono::steady_clock::now() - start;
    }

    // 收尾只拷贝预留计时槽，保证异常析构中不分配或写盘。
    void FrameDiagnosticsContext::finish() noexcept
    {
        record.timings = timing.snapshot();
    }
}
