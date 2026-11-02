#pragma once
#include "vision/observability/frame_record.hpp"
#include "application/observability_config.hpp"
namespace mark {
// A08：仅交付原图坐标的点线快照，渲染步骤无需了解算法阶段或缓存实现。
struct RenderInput {
 uint64_t frame_id{};
 bool sampled{false}, candidates_current{false};
 std::vector<std::vector<cv::Point2d>> candidates, support_arcs;
 std::vector<cv::Point2d> intersections;
 std::vector<ReasonEvent> evidence_events;
};
RenderInput makeRenderInput(const FrameRecord&);
cv::Mat renderDebugFrame(const cv::Mat&,const FrameResult&,const FrameRecord&,const RenderConfig&,const RenderInput&);
// 原显示可能复用旧overlay；每帧独立clone，仅绘当前合法结果，历史只允许文字。
cv::Mat renderDebugFrame(const cv::Mat&,const FrameResult&,const FrameRecord&,const RenderConfig&);
std::vector<ReasonEvent> validateRenderEvidence(const FrameRecord&);
}
