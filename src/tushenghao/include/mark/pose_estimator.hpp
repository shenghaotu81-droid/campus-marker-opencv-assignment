// raw适配与平面求解
#pragma once

#include "mark/detector_types.hpp"
#include "mark/pose_config.hpp"
#include "mark/pose_types.hpp"

namespace mark::pose
{
// 原输出没有位姿；独立无状态求解器读取 raw Detection，绝不写回原检测结果。
class PoseEstimator
{
public:
    explicit PoseEstimator(G2Config config);

    // MARK 入口：输入 FrameResult + 检测序号，内部用 orientation 把屏幕序转物理序(3D模型点：P0 固定是原点，P1 固定是 M1 外凸角)，再调通用求解
    PoseObservation estimate_mark(
        const FrameResult &raw, std::size_t detection_index_,
        cv::Size original_size, const CaptureStamp &stamp_,
        const std::optional<PosePrior> &prior = std::nullopt) const;

    // 通用 PnP：直接输入 4 对 2D-3D 点，输出 PoseObservation
    PoseObservation estimate_planar(
        const std::array<cv::Point3d, 4> &object_points_m,
        const std::array<cv::Point2d, 4> &image_points,
        cv::Size original_size, const CaptureStamp &stamp_,
        const std::string &target_id_,
        const std::optional<PosePrior> &prior = std::nullopt) const;

    const G2Config &config() const noexcept { return config_; }

private:
    G2Config config_;
};
} // namespace mark::pose