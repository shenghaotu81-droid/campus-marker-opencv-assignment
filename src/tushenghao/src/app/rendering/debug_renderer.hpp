#pragma once
#include "vision/types/detector_types.hpp"
#include "vision/types/pose_types.hpp"
#include "vision/types/tracking_types.hpp"
#include "vision/config/pose_config.hpp"
namespace mark {
// 只读主线输出；返回独立图像，不改变检测、求解或跟踪状态。
cv::Mat renderDebugFrame(const cv::Mat&, const FrameResult&,
    const std::vector<pose::PoseObservation>&, const std::vector<tracking::TargetTrack>&,
    const pose::G2Config *camera = nullptr);
}
