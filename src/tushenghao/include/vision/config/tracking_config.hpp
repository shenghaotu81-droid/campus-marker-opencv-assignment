#pragma once
#include "vision/types/tracking_types.hpp"
#include <filesystem>

namespace mark::tracking
{
    // 当前链只输出相机坐标的位置/表观速度。R/Q是模型，不是测量资格阈值。
    // tracker 只输出相机坐标系下的位置和表观速度（不输出姿态）；R/Q 是卡尔曼滤波的模型参数（决定滤波器多信任量测、多信任模型），不是“量测合格不合格”的门槛（合格不合格由 NIS 门定）
    // R/Q 模型参数
    struct TrackingProfile
    {
        // R，测量噪声协方差（3×3，单位 m²）。"mse" = 均方误差。默认值是零矩阵（Matx33d::zeros()）
        cv::Matx33d camera_mse_m2_{cv::Matx33d::zeros()}; // Matx33d::zeros() 就是造一个 3×3 零矩阵
        // acceleration_psd_m2ps3_——q，过程噪声功率谱密度（加速度噪声），单位 m²/s³
        // initial_velocity_variance_m2ps2_——Q，初始速度方差（初始速度估计多不确定），单位 m²/s²
        // {} 默认 0；单位体现在变量名中
        double acceleration_psd_m2ps3_{}, initial_velocity_variance_m2ps2_{};
    };

    // 顶层配置
    // 追踪配置：source、target_class、stamp_uncertainty_semantics、可选的 profile
    struct TrackingConfig
    {
        // source_——跟踪源，默认 "MARK"
        // target_class_——目标类别，默认 "mark-white-structure-v1"（和 pose.yaml 里的 mark_target_id_ 是同一个串；两者可比对）
        std::string source_{"MARK"}, target_class_{"mark-white-structure-v1"};
        // 开关（准备删掉）；表示时间不确定度（time_uncertainty_s_，时间戳多不确定，单位秒）数值语义（"absolute_bound"，表示绝对边界，可用于R/P计算；"unknown"，表示未知,按0处理）
        std::string stamp_uncertainty_semantics_{"unknown"};
        // R/Q profile
        std::optional<TrackingProfile> profile_;
    };

    // 自由函数，声明在头文件
    TrackingConfig load_tracking_config(const std::filesystem::path &);
}
