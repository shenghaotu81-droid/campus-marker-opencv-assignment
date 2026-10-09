#include "pipeline/frame_sequence.hpp"

namespace mark
{
    bool validateFrameStamp(const FrameStamp &c, const std::optional<FrameStamp> &p,
                            std::string &reason)
    {
        if (c.timestamp_us < 0 || c.time_source != TimestampSource::Unknown)
            reason = "INVALID_STAMP";
        else if (p && (c.frame_id <= p->frame_id || c.timestamp_us < p->timestamp_us))
            reason = "INVALID_SEQUENCE";
        else
        {
            reason.clear();
            return true;
        }
        return false;
    }
}
