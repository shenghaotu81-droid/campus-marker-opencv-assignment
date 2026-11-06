#pragma once
#include "vision/types/pose_types.hpp"
namespace mark::pose::detail
{
    bool valid_quad(const std::array<cv::Point2d, 4> &, cv::Size);
    std::optional<PoseCandidate> evaluate_pose(const RigidPose &, const std::array<cv::Point3d, 4> &,
                                               const std::array<cv::Point2d, 4> &, const cv::Mat &, const cv::Mat &);
}
