#pragma once
#include "vision/types/pose_types.hpp"
namespace mark::tracking
{
    using Vec6 = cv::Vec<double, 6>;
    using Mat6 = cv::Matx<double, 6, 6>;
    enum class TrackState {
        Measured, Coasting
    };
    struct TrackingFrame
    {
        pose::CaptureStamp stamp_;
        std::vector<pose::PoseObservation> observations_;
    };
    struct TargetTrack
    {
        uint64_t track_id_{};
        TrackState state_{TrackState::Coasting};
        pose::CaptureStamp last_measurement_stamp_;
        int64_t state_time_us_{};
        std::optional<cv::Vec3d> position_m_, velocity_mps_;
        std::optional<Mat6> covariance_;
    };
    struct TrackingResult
    {
        pose::CaptureStamp input_stamp_;
        bool input_accepted_{false};
        std::vector<TargetTrack> tracks_;
        std::vector<std::string> events_;
        std::optional<double> association_nis_;
    };
    const char *track_state_name(TrackState) noexcept;
}
