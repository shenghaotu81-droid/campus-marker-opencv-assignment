#pragma once
#include "core/diagnostics_types.hpp"

namespace mark
{
    // 原阶段工具缺计时状态；所有入口共用受控槽，重复开始与覆盖主动拒绝。
    class FrameTiming
    {
      public:
        explicit FrameTiming(bool enabled = false);
        void initialize(bool enabled, ExecutionScope scope);
        void markStatus(Stage, TimingStatus);
        void addMeasured(Stage, std::chrono::nanoseconds);
        void replaceProcessTotal(std::chrono::nanoseconds);

        const TimingSnapshot &snapshot() const noexcept
        {
            return slots_;
        }

        bool begin(Stage);
        void end(Stage, std::chrono::nanoseconds) noexcept;

      private:
        TimingSnapshot slots_{};
        std::array<bool, 9> active_{}, entered_{};
        bool enabled_{false};
    };

    // 析构只写预留槽，无分配/IO/异常；关闭时不读取steady_clock。
    class ScopedStageTimer
    {
      public:
        ScopedStageTimer(FrameTiming &, Stage);
        ~ScopedStageTimer() noexcept;
        ScopedStageTimer(const ScopedStageTimer &) = delete;
        ScopedStageTimer &operator=(const ScopedStageTimer &) = delete;

      private:
        FrameTiming &timing_;
        Stage stage_;
        bool running_;
        std::chrono::steady_clock::time_point started_{};
    };

    struct TimingStatistics
    {
        size_t n{};
        std::optional<double> mean_ns, median_ns, p95_ns, p99_ns, max_ns;
    };

    TimingStatistics summarizeDurations(const std::vector<std::chrono::nanoseconds> &);
}
