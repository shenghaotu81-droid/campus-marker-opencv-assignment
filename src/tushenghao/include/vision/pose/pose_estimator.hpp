#pragma once
#include "vision/types/detector_types.hpp"
#include "vision/config/pose_config.hpp"
#include "vision/types/pose_types.hpp"
#include <opencv2/core.hpp>
#include <array>

namespace mark::pose
{
    class PoseEstimator
    {
      public:
        // 构造函数，按值拿一份 PoseConfig；实现：config_(std::move(c))，把配置移进成员
        explicit PoseEstimator(PoseConfig);
        // MARK 专用入口。
        // 参数：检测器的 FrameResult（检到的角点）、size_t（第几个检测——FrameResult 里可能有多个）、cv::Size（图像尺寸）、CaptureStamp（帧的时间戳/元数据）
        // 返回 PoseObservation：stamp_（CaptureStamp：会话/相机/时钟、帧号、接收时间微秒、曝光、时间不确定度）、source_（检测器的 source，哪来的）、target_id_（MARK 的 target_id，目标 id）、detection_index_（第几个检测）、estimate_（名义解；std::optional<PoseEstimate>——有值 = ESTIMATED，没值 = UNAVAILABLE；PoseEstimate 里是 RigidPose（rvec_ + t_m_）、rmse_px_、translation_spread_m2_（S））、reason_（失败原因；字符串）
        // 实现：它从 FrameResult 里取出第 index 个检测的角点，调 estimate_planar(config_.mark_points_m_, pixels, size, stamp, config_.mark_target_id_)
        // MARK 专用包装，先取角点、再调通用核心
        PoseObservation estimate_mark(const detector::FrameResult &, size_t, cv::Size,
                                      const CaptureStamp &) const;
        // 通用平面位姿核心（IPPE）
        // 参数：4 个 3D 模型点（Point3d，米）、4 个 2D 像点（Point2d，像素）、CaptureStamp、std::string（目标 id/标签）
        // 实现：调 cv::solvePnPGeneric(..., SOLVEPNP_IPPE)，拿到两个候选解，选 RMSE 最小的当名义解、算 S，包成 PoseObservation
        PoseObservation estimate_planar(const std::array<cv::Point3d, 4> &,
                                        const std::array<cv::Point2d, 4> &, cv::Size,
                                        const CaptureStamp &) const;

      private:
        // 配置直接存成成员，没有 pimpl
        // 和 Detector 的区别：Detector 用 pimpl（Impl + unique_ptr）是因为它有内部状态（last_input_、original_size_）和要隐藏的实现细节；
        // PoseEstimator 基本无状态（只有配置 + 纯计算），直接存配置即可
        // 也没有 = delete——可拷贝（拷一份即拷一份配置）；而Detector 不可拷贝（有状态）
        PoseConfig config_;
    };
}
