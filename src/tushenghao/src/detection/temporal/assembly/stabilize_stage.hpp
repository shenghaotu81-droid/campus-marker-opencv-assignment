// process与audit共享一次decode的装配结果，生产显示语义源恒空。
#pragma once
#include "pipeline/decode/decode_stage.hpp"
#include "temporal/stabilization/temporal_stabilizer.hpp"
#include "temporal/display/display_history.hpp"

namespace mark
{
    class FrameDiagnosticsContext;
    FrameResult finalizeDecodedFrame(const DecodeStageResult &, const FrameStamp &, cv::Size,
                                     TemporalStabilizer &, DisplayHistory &,
                                     FrameDiagnosticsContext *);
    FrameResult finalizeDecodedFrame(const DecodeStageResult &, const FrameStamp &, cv::Size,
                                     TemporalStabilizer &, DisplayHistory &);
}
