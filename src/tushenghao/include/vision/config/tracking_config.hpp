#pragma once
#include "vision/types/tracking_types.hpp"
#include <filesystem>
namespace mark::tracking
{
// 当前链只输出相机坐标的位置/表观速度。R/Q是模型，不是测量资格阈值。
    struct TrackingProfile
    {
        cv::Matx33d camera_mse_m2_{cv::Matx33d::zeros()};
        double acceleration_psd_m2ps3_{}, initial_velocity_variance_m2ps2_{};
    };
    struct TrackingConfig
    {
        std::string source_{"MARK"}, target_class_{"mark-white-structure-v1"};
        std::string stamp_uncertainty_semantics_{"unknown"};
        std::optional<TrackingProfile> profile_;
    };
    TrackingConfig load_tracking_config(const std::filesystem::path &);
    void validate_tracking_config(const TrackingConfig &);
}
