#pragma once

#include "mark/pose_types.hpp"
#include <optional>
#include <string>
#include <vector>

namespace mark::tracking
{
using Vec6 = cv::Vec<double, 6>;
using Mat6 = cv::Matx<double, 6, 6>;

enum class TrackState { Measured, Coasting, Lost };
enum class CoordinateMode { CameraRelative, LocalWorld };
enum class InputOrigin { TypedMemory, Synthetic, LegacyG2Replay };

// G2 的 SE3 数组尚无排列约定；独立旁路明确只提供“相机系位置误差二阶矩”。
struct PositionNoise
{
    std::size_t detection_index_{0};
    cv::Matx33d camera_mse_m2_{cv::Matx33d::zeros()};
    std::string evidence_id_;
    bool hardware_verified_{false};
};

// 由外部里程/安装适配器提供同曝光时刻的 ^W T_C；本模块不读取 IMU/轮速驱动。
// 6维误差顺序[平移W xyz,小旋转W xyz]，旋转绕相机原点；未知协方差保持空。
struct CameraTransform
{
    pose::CaptureStamp stamp_;
    std::string world_frame_id_;
    cv::Matx33d R_world_camera_{cv::Matx33d::eye()};
    cv::Vec3d t_world_camera_m_{0, 0, 0};
    std::optional<Mat6> covariance_;
    bool hardware_verified_{false};
};

// 空 observations 也必须每帧调用；否则遮挡期间时钟/协方差/LOST不会推进。
struct TrackingFrame
{
    pose::CaptureStamp stamp_;
    InputOrigin origin_{InputOrigin::TypedMemory};
    std::vector<pose::PoseObservation> observations_;
    std::vector<PositionNoise> position_noise_;
    std::optional<CameraTransform> camera_transform_;
};

struct TargetTrack
{
    std::uint64_t track_id_{0};
    TrackState state_{TrackState::Lost};
    std::string source_;
    std::string target_class_;
    std::string coordinate_frame_;
    // 相机模式为 d(p_camera)/dt，含本车平移/旋转表观运动，绝非世界目标速度。
    std::string velocity_meaning_;
    pose::CaptureStamp last_measurement_stamp_;
    std::optional<std::size_t> last_detection_index_;
    std::int64_t state_time_us_{0};
    std::optional<cv::Vec3d> position_m_;
    std::optional<cv::Vec3d> velocity_mps_;
    std::optional<Mat6> covariance_;
    bool confirmed_{false};
    bool time_bounded_{false};
    bool model_hardware_verified_{false};
    bool qualified_for_g5_{false};
    std::optional<double> observation_age_s_;
    std::optional<double> input_time_bound_s_;
    // 协方差是给定噪声/时间模型下的估计；非无条件物理保证、非置信度概率。
    std::string uncertainty_scope_{"MODEL_CONDITIONAL"};
};

struct TrackingResult
{
    pose::CaptureStamp input_stamp_;
    bool input_accepted_{false};
    bool ready_{false};
    // 保留集合接口，本期最多一个；LOST记录不携带旧位置/速度作为新测量。
    std::vector<TargetTrack> tracks_;
    std::vector<std::string> events_;
    std::optional<double> association_nis_;
};

struct TargetForecast
{
    bool available_{false};
    std::uint64_t track_id_{0};
    std::string coordinate_frame_;
    std::string session_id_;
    std::string clock_domain_;
    bool time_bounded_{false};
    std::int64_t state_time_us_{0};
    std::int64_t last_measurement_us_{0};
    std::optional<cv::Vec3d> position_m_;
    std::optional<cv::Vec3d> velocity_mps_;
    std::optional<Mat6> covariance_;
    std::string reason_;
};

const char *track_state_name(TrackState state) noexcept;
} // namespace mark::tracking
