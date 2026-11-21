#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <opencv2/core.hpp>

namespace mark::pose
{
    enum class PixelDomain
    {
        Unknown,
        RawDistorted
    };

    struct CaptureStamp
    {
        std::string session_id_, camera_id_, clock_domain_; // "这个时间戳是按哪把尺子量的"——时间戳的时钟来源
        // 这帧的像素坐标属于哪个"域"——是原始带畸变的图像坐标，还是别的什么
        PixelDomain pixel_domain_{PixelDomain::Unknown};
        // 帧号：排序/去重
        uint64_t frame_id_{};
        // 收到这帧的时刻（微秒）：离线是 frame_id / FPS 推导出来的名义值；板子上是真实到达时间
        int64_t received_us_{};
        // 曝光发生的时刻（微秒），量测真正的时刻；离线永远是空的（视频文件没有曝光时间戳），只有板子相机才有
        std::optional<int64_t> exposure_us_;
        // 采样时间的不确定度（秒）
        // 用来放大 R（time_augmented_noise）和传给 predict 的 bound
        // 离线空
        std::optional<double> time_uncertainty_s_;
    };

    struct RigidPose
    {
        cv::Vec3d rvec_{0, 0, 0}, t_m_{0, 0, 0};
    };

    struct PoseEstimate
    {
        RigidPose pose_;
        double rmse_px_{};
        // 合法其他解相对名义平移的加权二阶矩；Tracking把它加到profile R。
        // 不是完整SE3协方差，也不是经过标定的后验概率。
        cv::Matx33d translation_spread_m2_{cv::Matx33d::zeros()};
    };

    struct PoseObservation
    {
        CaptureStamp stamp_;
        std::string source_, target_id_;
        size_t detection_index_{};
        // 有值仅表示有数学合法的名义估计，不表示唯一性或控制资格。
        std::optional<PoseEstimate> estimate_;
        std::string reason_;
    };

    // 估计状态的字符串化：ESTIMATED / UNAVAILABLE
    inline const char *pose_status_name(const PoseObservation &p) noexcept
    {
        return p.estimate_ ? "ESTIMATED" : "UNAVAILABLE"; // 现算，返回指向字符串字面量 const char[N] 的指针
    }

    struct TagObservation
    {
        CaptureStamp stamp_;
        int id_{};
        double side_m_{};
        std::array<cv::Point2d, 4> corners_;
    };
}
