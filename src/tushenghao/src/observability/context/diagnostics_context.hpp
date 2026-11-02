#pragma once
#include "timing/timing.hpp"
#include "vision/observability/stage_details.hpp"

namespace mark
{
    struct DiagnosticsRequest
    {
        std::string run_id;
        bool timing_enabled{false}, selected{false};
        ExecutionScope scope{ExecutionScope::Full};
        DiagnosticsLevel level{DiagnosticsLevel::Summary};
        std::string timestamp_recipe{"input"};
    };

    // 每个Detector实例拥有上下文；不使用全局/tls，旧公共消费无prepare仍能安全处理。
    class FrameDiagnosticsContext
    {
      public:
        DiagnosticsRequest request;
        FrameRecord record;
        FrameTiming timing;
        std::shared_ptr<StageDetails> details;
        void begin(const FrameInput &, const DiagnosticsRequest &);
        void event(Stage, ReasonCode, std::string,
                   std::optional<uint64_t> hypothesis = std::nullopt);
        void resets(const ResetSnapshot &, std::optional<uint64_t> source);
        void output(const FrameResult &);
        void finish() noexcept;
    };
}
