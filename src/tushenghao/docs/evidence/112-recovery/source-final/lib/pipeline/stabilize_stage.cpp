#include "pipeline/stabilize_stage.hpp"
#include "pipeline/diagnostics_context.hpp"
#include <stdexcept>

namespace mark
{
    FrameResult finalizeDecodedFrame(const DecodeStageResult &decoded, const FrameStamp &stamp,
                                     cv::Size size, TemporalStabilizer &temporal,
                                     DisplayHistory &display)
    {
        return finalizeDecodedFrame(decoded, stamp, size, temporal, display, nullptr);
    }

    // 原非法分支先reset再读诊断，丢掉真实失败；先复制本次结果，再清历史，不改算法调用顺序。
    FrameResult finalizeDecodedFrame(const DecodeStageResult &decoded, const FrameStamp &stamp,
                                     cv::Size size, TemporalStabilizer &temporal,
                                     DisplayHistory &display, FrameDiagnosticsContext *context)
    {
        std::optional<ScopedStageTimer> timer;
        if (context)
        {
            timer.emplace(context->timing, Stage::Stabilize);
            auto previous = temporal.takeResetSnapshot();
            if (context->request.scope == ExecutionScope::Temporal)
                context->resets(previous, std::nullopt);
        }
        FrameResult result{};
        result.frame_id = stamp.frame_id;
        result.timestamp_us = stamp.timestamp_us;
        result.status = decoded.status;
        result.diagnostics = decoded.diagnostics;
        if (decoded.status == Status::NOT_READY || decoded.status == Status::INVALID_INPUT)
        {
            temporal.reset(ResetReason::InvalidSequence);
            display.reset(ResetReason::InvalidSequence);
            if (context)
            {
                auto resets = temporal.takeResetSnapshot();
                if (context->request.scope == ExecutionScope::Temporal)
                    context->resets(resets, stamp.frame_id);
            }
            return result;
        }
        // 内部编排不一致须显式失败；不能拿平滑掩盖status与原始集合的矛盾。
        if ((decoded.status == Status::DETECTED) != !decoded.detections.empty())
        {
            temporal.reset(ResetReason::InvalidSequence);
            display.reset(ResetReason::InvalidSequence);
            if (context)
            {
                context->record.run_failed = true;
                context->event(Stage::Stabilize, ReasonCode::OtherRecordedReason,
                               "DECODE_STATUS_PAYLOAD_MISMATCH");
                auto resets = temporal.takeResetSnapshot();
                if (context->request.scope == ExecutionScope::Temporal)
                    context->resets(resets, stamp.frame_id);
            }
            throw std::logic_error("DECODE_STATUS_PAYLOAD_MISMATCH");
        }
        auto stable = temporal.update(decoded.detections, stamp, size);
        if (context)
        {
            if (context->details)
                context->details->temporal = stable.diagnostics;
            context->record.counts.tracks = stable.tracks.size();
            for (const auto &reason : {stable.diagnostics.association.reason,
                                       stable.diagnostics.reset_or_fallback_reason})
                if (!reason.empty())
                {
                    ReasonCode code = ReasonCode::OtherRecordedReason;
                    if (reason == "HISTORY_EXPIRED")
                        code = ReasonCode::HistoryExpired;
                    else if (reason == "ASSOCIATION_FAILED")
                        code = ReasonCode::AssociationFailed;
                    else if (reason == "ASSOCIATION_AMBIGUOUS")
                        code = ReasonCode::AssociationAmbiguous;
                    else if (reason == "CORRESPONDENCE_AMBIGUOUS")
                        code = ReasonCode::CorrespondenceAmbiguous;
                    else if (reason == "ZERO_DT")
                        code = ReasonCode::ZeroDt;
                    else if (reason == "SMOOTHING_DEVIATION")
                        code = ReasonCode::SmoothingRejected;
                    context->event(Stage::Stabilize, code, reason);
                }
            auto resets = temporal.takeResetSnapshot();
            if (context->request.scope == ExecutionScope::Temporal)
                context->resets(resets, stamp.frame_id);
        }
        result.status = stable.status;
        result.diagnostics.push_back(stable.diagnostics.association.reason);
        result.diagnostics.push_back(stable.diagnostics.reset_or_fallback_reason);
        if (result.status == Status::INVALID_INPUT)
        {
            temporal.reset(ResetReason::InvalidSequence);
            display.reset(ResetReason::InvalidSequence);
            // update已记录同一非法调用的语义reset，finalize清三历史不再重复计一次。
            if (context)
                temporal.takeResetSnapshot();
            return result;
        }
        if (stable.diagnostics.reset_or_fallback_reason == "INPUT_CHANGED")
            display.reset(ResetReason::InputChanged);
        result.detections = decoded.detections;
        result.tracks = std::move(stable.tracks);
        result.display_state = display.update(std::nullopt, stamp);
        return result;
    }
}
