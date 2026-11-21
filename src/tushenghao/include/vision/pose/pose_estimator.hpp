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
        // 参数为检测结果、检测索引、图像尺寸和帧戳。
        // 观测携带帧戳、来源、检测索引和可选名义解。名义解包含位姿、重投影 RMSE 和位置分歧。
        // 有名义解时报告 ESTIMATED，否则报告 UNAVAILABLE，失败时可附原因。
        // 按检测索引取角点，按物理顺序重排后交给 estimate_planar。
        // MARK 专用包装，先取角点、再调通用核心
        PoseObservation estimate_mark(const detector::FrameResult &, size_t, cv::Size,
                                      const CaptureStamp &) const;
        // 通用平面位姿核心（IPPE）
        // 参数为四个米制模型点、四个像素角点、图像尺寸和帧戳。
        // 最多取得两个候选，逐个复核后选择名义解并计算位置分歧。
        PoseObservation estimate_planar(const std::array<cv::Point3d, 4> &,
                                        const std::array<cv::Point2d, 4> &, cv::Size,
                                        const CaptureStamp &) const;

      private:
        // 配置直接存成成员，没有 pimpl
        // Detector 用 pimpl 隐藏内部实现及 last_input_ 帧序状态。
        // PoseEstimator 基本无状态（只有配置 + 纯计算），直接存配置即可
        // 也没有 = delete——可拷贝（拷一份即拷一份配置）；而Detector 不可拷贝（有状态）
        PoseConfig config_;
    };
}
