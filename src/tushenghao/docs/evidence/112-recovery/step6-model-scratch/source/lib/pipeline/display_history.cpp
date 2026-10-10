#include "pipeline/display_history.hpp"
#include "pipeline/frame_sequence.hpp"
#include <stdexcept>

namespace mark
{
    DisplayHistory::DisplayHistory(DisplayHistoryConfig c) : config_(c)
    {
        if (c.max_hold_frames < 0)
            throw std::invalid_argument("max_hold_frames must be non-negative");
    }

    void DisplayHistory::reset(ResetReason) noexcept
    {
        source_.reset();
        missing_calls_ = 0;
        previous_.reset();
    }

    // age按有效调用次数计，避免跳帧号把5次桥接误作帧号差。
    std::optional<DisplayState>
    DisplayHistory::update(const std::optional<SemanticDisplaySample> &current,
                           const FrameStamp &stamp)
    {
        std::string reason;
        if (!validateFrameStamp(stamp, previous_, reason) ||
            (current && (current->source_frame_id != stamp.frame_id ||
                         current->source_timestamp_us != stamp.timestamp_us)))
        {
            reset(ResetReason::InvalidSequence);
            return std::nullopt;
        }
        previous_ = stamp;
        if (!config_.enabled)
        {
            source_.reset();
            missing_calls_ = 0;
            if (current)
                return DisplayState{current->source_frame_id, 0, false, current->value};
            return std::nullopt;
        }
        if (current)
        {
            source_ = current;
            missing_calls_ = 0;
            return DisplayState{current->source_frame_id, 0, false, current->value};
        }
        if (!source_)
            return std::nullopt;
        ++missing_calls_;
        if (missing_calls_ > static_cast<uint64_t>(config_.max_hold_frames))
        {
            source_.reset();
            missing_calls_ = 0;
            return std::nullopt;
        }
        return DisplayState{source_->source_frame_id, missing_calls_, true, source_->value};
    }
}
