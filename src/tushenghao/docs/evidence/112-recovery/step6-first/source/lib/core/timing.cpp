#include "core/timing.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace mark
{
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

    FrameTiming::FrameTiming(bool enabled)
    {
        initialize(enabled, ExecutionScope::Full);
    }

    void FrameTiming::initialize(bool enabled, ExecutionScope scope)
    {
        enabled_ = enabled;
        active_.fill(false);
        entered_.fill(false);
        for (size_t i = 0; i < 9; ++i)
        {
            bool inside =
                i == 1 || i == 2 || (i == 3 && scope != ExecutionScope::Geometry) ||
                (i == 4 && (scope == ExecutionScope::Full || scope == ExecutionScope::Temporal)) ||
                (i == 5 && scope == ExecutionScope::Full);
            slots_[i] = {static_cast<Stage>(i),
                         inside ? TimingStatus::SKIPPED : TimingStatus::NOT_EXECUTED, std::nullopt};
        }
    }

    void FrameTiming::markStatus(Stage stage, TimingStatus status)
    {
        auto i = static_cast<size_t>(stage);
        if (active_.at(i) || entered_.at(i) || status == TimingStatus::MEASURED)
            throw std::logic_error("TIMING_STATUS_OVERWRITE");
        slots_[i].status = status;
        slots_[i].elapsed.reset();
    }

    bool FrameTiming::begin(Stage stage)
    {
        auto i = static_cast<size_t>(stage);
        if (active_.at(i) || entered_.at(i))
            throw std::logic_error("TIMING_DUPLICATE_STAGE");
        entered_[i] = true;
        active_[i] = enabled_;
        slots_[i].status = enabled_ ? TimingStatus::MEASURED : TimingStatus::DISABLED;
        return enabled_;
    }

    void FrameTiming::end(Stage stage, std::chrono::nanoseconds elapsed) noexcept
    {
        auto i = static_cast<size_t>(stage);
        slots_[i].elapsed = elapsed;
        active_[i] = false;
    }

    void FrameTiming::addMeasured(Stage stage, std::chrono::nanoseconds elapsed)
    {
        if (elapsed.count() < 0)
            throw std::invalid_argument("NEGATIVE_DURATION");
        if (!enabled_)
            throw std::logic_error("TIMING_DISABLED");
        if (begin(stage))
            end(stage, elapsed);
    }

    void FrameTiming::replaceProcessTotal(std::chrono::nanoseconds elapsed)
    {
        auto i = static_cast<size_t>(Stage::ProcessTotal);
        if (!enabled_ || active_[i] || elapsed.count() < 0)
            throw std::logic_error("INVALID_PUBLIC_TOTAL_REPLACEMENT");
        entered_[i] = true;
        slots_[i] = {Stage::ProcessTotal, TimingStatus::MEASURED, elapsed};
    }

    ScopedStageTimer::ScopedStageTimer(FrameTiming &t, Stage s)
        : timing_(t), stage_(s), running_(t.begin(s))
    {
        if (running_)
            started_ = std::chrono::steady_clock::now();
    }

    ScopedStageTimer::~ScopedStageTimer() noexcept
    {
        if (running_)
            timing_.end(stage_, std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::steady_clock::now() - started_));
    }

    // 使用nearest-rank；首帧/慢帧均保留，偶数中位数取中间两项平均，空统计保持null。
    TimingStatistics summarizeDurations(const std::vector<std::chrono::nanoseconds> &input)
    {
        TimingStatistics s;
        s.n = input.size();
        if (input.empty())
            return s;
        auto v = input;
        std::sort(v.begin(), v.end());
        long double sum = 0;
        for (auto d : v)
        {
            if (d.count() < 0)
                throw std::invalid_argument("NEGATIVE_DURATION");
            sum += d.count();
        }
        s.mean_ns = double(sum / v.size());
        s.median_ns =
            v.size() % 2
                ? double(v[v.size() / 2].count())
                : (double(v[v.size() / 2 - 1].count()) + double(v[v.size() / 2].count())) / 2;
        s.p95_ns = double(v[size_t(std::ceil(.95 * v.size())) - 1].count());
        s.p99_ns = double(v[size_t(std::ceil(.99 * v.size())) - 1].count());
        s.max_ns = double(v.back().count());
        return s;
    }
}
