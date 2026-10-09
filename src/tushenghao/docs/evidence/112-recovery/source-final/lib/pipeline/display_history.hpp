// 文字桥接只接受独立语义，不接收或保存角点/方向/Detection。
#pragma once
#include "pipeline/temporal_types.hpp"

namespace mark
{
    struct DisplayHistoryConfig
    {
        bool enabled{false};
        int max_hold_frames{5};
    };

    class DisplayHistory
    {
      public:
        explicit DisplayHistory(DisplayHistoryConfig);
        std::optional<DisplayState> update(const std::optional<SemanticDisplaySample> &,
                                           const FrameStamp &);
        void reset(ResetReason) noexcept;

      private:
        DisplayHistoryConfig config_;
        std::optional<SemanticDisplaySample> source_;
        uint64_t missing_calls_{};
        std::optional<FrameStamp> previous_;
    };
}
