#pragma once

#include "mark/tracking_types.hpp"
#include <filesystem>

namespace mark::tracking
{
// Q/R是模型二阶矩，不是“阈值”；门控值才由同口径统计量 mean+3*sample_std 产生。
struct TrackingProfile
{
    cv::Matx33d camera_mse_m2_{cv::Matx33d::zeros()};
    double acceleration_psd_m2ps3_{0};
    double initial_velocity_variance_m2ps2_{0};
    double association_nis_limit_{0};
    double bootstrap_nis_limit_{0};
    double position_std_limit_m_{0};
    double velocity_std_limit_mps_{0};
    double max_dt_s_{0};
    std::optional<double> validated_time_bound_s_;
    std::string evidence_id_;
    std::string camera_id_;
    std::string basis_;
    std::string coordinate_mode_{"camera_relative"};
    std::string time_policy_{"nominal_research"};
    bool hardware_verified_{false};
};

struct TrackingConfig
{
    std::string source_{"MARK"};
    std::string target_class_{"mark-white-structure-v1"};
    CoordinateMode coordinate_mode_{CoordinateMode::CameraRelative};
    std::string world_frame_id_{"local-world"};
    // nominal_research允许数值研究，始终不赋予时间资格；bounded_only要求真曝光与有界误差。
    std::string time_policy_{"nominal_research"};
    std::string stamp_uncertainty_semantics_{"unknown"};
    std::optional<double> max_time_uncertainty_s_;
    std::optional<TrackingProfile> profile_;
};

TrackingProfile load_tracking_profile(const std::filesystem::path &path);
void write_tracking_profile(const TrackingProfile &profile, const std::filesystem::path &path);
TrackingConfig load_tracking_config(const std::filesystem::path &path);
void validate_tracking_profile(const TrackingProfile &profile);
void validate_tracking_config(const TrackingConfig &config);
} // namespace mark::tracking
