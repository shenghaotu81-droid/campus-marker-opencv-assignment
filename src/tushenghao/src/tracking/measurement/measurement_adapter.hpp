#pragma once
#include "vision/config/tracking_config.hpp"
namespace mark::tracking::detail
{
    struct PositionMeasurement
    {
        cv::Vec3d position;
        cv::Matx33d R;
    };
    int64_t sample_time(const pose::CaptureStamp &);
    bool same_sample(const pose::CaptureStamp &, const pose::CaptureStamp &);
    std::vector<PositionMeasurement> adapt_measurements(const TrackingFrame &, const TrackingConfig &);
    cv::Matx33d time_augmented_noise(const cv::Matx33d &, const cv::Vec3d &, const cv::Matx33d &, double, double);
}
