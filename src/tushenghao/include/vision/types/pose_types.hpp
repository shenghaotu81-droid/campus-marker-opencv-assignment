#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
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
    enum class PoseStatus {
        InvalidInput, NotDetected, NoSolution, Ambiguous, Unique
    };
    struct PoseCandidate
    {
        RigidPose pose_;
        double rmse_px_{};
    };
    struct PoseObservation
    {
        CaptureStamp stamp_;
        std::string source_, target_id_;
        size_t detection_index_{};
        PoseStatus status_{PoseStatus::InvalidInput};
        std::vector<PoseCandidate> candidates_;
        std::optional<size_t> selected_index_;
        std::string reason_;
        // selected只是名义最小重投影支；有第二解仍标Ambiguous，G4同时消费两支分歧。
        const PoseCandidate *measurement() const
        {
            return selected_index_ && *selected_index_ < candidates_.size() ? &candidates_[*selected_index_] : nullptr;
        }

    };
    struct TagObservation
    {
        CaptureStamp stamp_;
        int id_{};
        double side_m_{};
        std::array<cv::Point2d, 4> corners_;
    };
    const char *pose_status_name(PoseStatus) noexcept;
}
