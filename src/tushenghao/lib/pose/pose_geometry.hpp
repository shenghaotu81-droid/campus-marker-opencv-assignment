// 小范围数学内部声明
#pragma once

#include "mark/pose_types.hpp"

namespace mark::pose::detail
{
// 只做数学合法性检查；像素可用域阈值另由显式预算控制。
// 输入检查（解算前验时间戳、四边形合法）
bool valid_stamp(const CaptureStamp &stamp_);
bool valid_quad(const std::array<cv::Point2d, 4> &points, cv::Size size);
// 算面积（给最小面积预算用）
double quad_area(const std::array<cv::Point2d, 4> &points);
// 算两个旋转的差（prior 比较、测试用）
double rotation_distance(const cv::Vec3d &a, const cv::Vec3d &b);
// 核心，对结果独立复核
PoseCandidate evaluate_pose(const RigidPose &pose_,
                            const std::array<cv::Point3d, 4> &object,
                            const std::array<cv::Point2d, 4> &image,
                            const cv::Mat &K_, const cv::Mat &dist_);
} // namespace mark::pose::detail