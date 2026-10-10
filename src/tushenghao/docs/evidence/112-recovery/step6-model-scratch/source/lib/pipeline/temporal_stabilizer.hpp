// 选择参考与平滑历史分开；empty只留短期选择，恢复从原始点重建。
#pragma once
#include "pipeline/temporal_geometry.hpp"
#include "core/diagnostics_types.hpp"

namespace mark
{
    double computeTimeConstantSeconds(double reference_dt_ms, double reference_alpha);
    double computeCurrentWeight(double dt_seconds, double tau_seconds);

    class TemporalStabilizer
    {
      public:
        explicit TemporalStabilizer(TemporalConfig);
        TemporalResult update(const std::vector<Detection> &, const FrameStamp &,
                              cv::Size original_size);
        void reset(ResetReason) noexcept;

        const TemporalDiagnostics &diagnostics() const noexcept
        {
            return diagnostics_;
        }

        // 原reset抹掉诊断；audit在清理前保留本次详情，另取无分配原因快照。
        ResetSnapshot takeResetSnapshot() noexcept
        {
            auto snapshot = resets_;
            resets_ = {};
            return snapshot;
        }

      private:
        struct Reference
        {
            Detection raw;
            FrameStamp stamp;
        };

        struct Smoothing
        {
            Detection raw;
            std::array<cv::Point2d, 4> stable;
            FrameStamp stamp;
        };

        TemporalConfig config_;
        double tau_;
        std::optional<Reference> reference_;
        std::optional<Smoothing> smoothing_;
        std::optional<FrameStamp> last_input_;
        std::optional<cv::Size> size_;
        TemporalDiagnostics diagnostics_;
        ResetSnapshot resets_;
    };
}
