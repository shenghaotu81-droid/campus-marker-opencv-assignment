#pragma once
#include "pipeline/diagnostics_context.hpp"
#include "config/observability_config.hpp"
namespace mark {
// 原显示可能复用旧overlay；每帧独立clone，仅绘当前合法结果，历史只允许文字。
cv::Mat renderDebugFrame(const cv::Mat&,const FrameResult&,const FrameRecord&,const RenderConfig&);
std::vector<ReasonEvent> validateRenderEvidence(const FrameRecord&);
}
