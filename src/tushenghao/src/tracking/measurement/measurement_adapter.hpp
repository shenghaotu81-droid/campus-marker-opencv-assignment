#pragma once

#include "vision/config/tracking_config.hpp"

namespace mark::tracking::detail
{
// 原G2位姿/协方差没有跟踪位置噪声约定；适配后固定为所选坐标系下的m与m²。
// detection_index只用于联回本帧证据，不能承担跨帧身份。
struct PositionMeasurement
{
    std::size_t detection_index_{0};
    cv::Vec3d position_m_{0, 0, 0};
    cv::Matx33d R_m2_{cv::Matx33d::zeros()};
    bool hardware_verified_{false};
};

// 时间与来源分别验证：缺曝光可研究，但不能借接收时刻获得真实时间资格。
std::int64_t sample_time(const pose::CaptureStamp &stamp);
bool valid_stamp(const pose::CaptureStamp &stamp);
bool same_sample(const pose::CaptureStamp &a, const pose::CaptureStamp &b);
bool time_bounded(const TrackingFrame &frame, const TrackingConfig &config);
// 把明确的绝对时偏界传播为局部模型MSE包络，避免把上界误写成高斯标准差。
cv::Matx33d time_augmented_noise(const cv::Matx33d &R, const cv::Vec3d &velocity,
    const cv::Matx33d &velocity_covariance, double absolute_bound_s, double acceleration_psd);
// 只转换当前Unique及其完整选中证据；世界模式还必须提供同曝光的相机变换。
std::vector<PositionMeasurement> adapt_measurements(
    const TrackingFrame &frame, const TrackingConfig &config, std::vector<std::string> &events);
} // namespace mark::tracking::detail
