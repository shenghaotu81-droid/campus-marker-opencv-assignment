#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <opencv2/core.hpp>
namespace mark::pose
{
    enum class PixelDomain {
        Unknown, RawDistorted
    };
    struct CaptureStamp
    {
        std::string session_id_, camera_id_, clock_domain_;
        PixelDomain pixel_domain_{PixelDomain::Unknown};
        uint64_t frame_id_{};
        int64_t received_us_{};
        std::optional<int64_t> exposure_us_;
        std::optional<double> time_uncertainty_s_;
        bool valid() const
        {
            return !session_id_.empty() && !camera_id_.empty() && !clock_domain_.empty() && pixel_domain_ == PixelDomain::RawDistorted && received_us_ >= 0 && (!exposure_us_ || (*exposure_us_ >= 0 && *exposure_us_ <= received_us_)) && (!time_uncertainty_s_ || (std::isfinite(*time_uncertainty_s_) && *time_uncertainty_s_ >= 0));
        }

    };
    struct RigidPose
    {
        cv::Vec3d rvec_{0, 0, 0}, t_m_{0, 0, 0};
    };
    struct PoseEstimate
    {
        RigidPose pose_;
        double rmse_px_{};
        // 合法其他解相对名义平移的加权二阶矩；G4把它加到profile R。
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
    inline const char *pose_status_name(const PoseObservation &p) noexcept
    {
        return p.estimate_ ? "ESTIMATED" : "UNAVAILABLE";
    }
    struct TagObservation
    {
        CaptureStamp stamp_;
        int id_{};
        double side_m_{};
        std::array<cv::Point2d, 4> corners_;
    };
}
