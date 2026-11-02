#include "vision/observability/frame_record.hpp"
namespace mark
{
    // A06/A12：记录枚举编码独立于计时器实现，协议字符串保持原值。
    void ResetSnapshot::add(ResetReason reason) noexcept
    {
        size_t i = reason == ResetReason::External       ? 0
                   : reason == ResetReason::InputChanged ? 1
                                                         : 2;
        if (counts[i] == std::numeric_limits<uint64_t>::max())
            saturated = true;
        else
            ++counts[i];
        last = reason;
    }

    const char *stageName(Stage s)
    {
        static constexpr const char *names[] = {"capture",   "preprocess", "detect",
                                                "decode",    "stabilize",  "process_total",
                                                "visualize", "wait",       "export"};
        return names[static_cast<size_t>(s)];
    }

    const char *timingStatusName(TimingStatus s)
    {
        static constexpr const char *names[] = {"MEASURED", "DISABLED", "SKIPPED",
                                                "NOT_IMPLEMENTED", "NOT_EXECUTED"};
        return names[static_cast<size_t>(s)];
    }

    const char *scopeName(ExecutionScope s)
    {
        static constexpr const char *names[] = {"full", "geometry", "decode", "temporal"};
        return names[static_cast<size_t>(s)];
    }

    const char *reasonName(ReasonCode s)
    {
        static constexpr const char *names[] = {"InputFormat",
                                                "InvalidStamp",
                                                "InvalidSequence",
                                                "BudgetMissing",
                                                "ClearlyIncomplete",
                                                "AssignmentInsufficient",
                                                "CornerRejected",
                                                "ScreenRejected",
                                                "ValidationRejected",
                                                "UnresolvedCompetition",
                                                "SemanticRejected",
                                                "PublishFloatRejected",
                                                "HistoryExpired",
                                                "AssociationFailed",
                                                "AssociationAmbiguous",
                                                "CorrespondenceAmbiguous",
                                                "ZeroDt",
                                                "SmoothingRejected",
                                                "ResetExternal",
                                                "ResetInputChanged",
                                                "ResetInvalidSequence",
                                                "NonFiniteField",
                                                "IoFailure",
                                                "OtherRecordedReason"};
        return names[static_cast<size_t>(s)];
    }

}
