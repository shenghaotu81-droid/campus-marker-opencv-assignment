#pragma once
#include "vision/types/tracking_types.hpp"
#include <filesystem>

namespace mark::tracking
{
    // 当前链只输出相机坐标的位置/表观速度。R/Q是模型，不是测量资格阈值。
    // R/Q 分别描述量测与过程噪声。跟踪按 NIS 排序选择量测，最小分数平局时跳过更新。
    // R/Q 模型参数
    struct TrackingProfile
    {
        // R，测量噪声协方差（3×3，单位 m²）。"mse" = 均方误差。默认值是零矩阵（Matx33d::zeros()）
        cv::Matx33d camera_mse_m2_{cv::Matx33d::zeros()}; // Matx33d::zeros() 就是造一个 3×3 零矩阵
        // acceleration_psd_m2ps3_——q，过程噪声功率谱密度（加速度噪声），单位 m²/s³
        // initial_velocity_variance_m2ps2_ 是初始 P 的速度块对角项。
        // {} 默认 0；单位体现在变量名中
        double acceleration_psd_m2ps3_{}, initial_velocity_variance_m2ps2_{};
    };

    // 顶层配置
    // 追踪配置包含量测来源 source 和可选的 profile。
    struct TrackingConfig
    {
        // source_——跟踪源，默认 "MARK"
        std::string source_{"MARK"};
        // R/Q profile
        std::optional<TrackingProfile> profile_;
    };

    // 自由函数，声明在头文件
    TrackingConfig load_tracking_config(const std::filesystem::path &);
}
