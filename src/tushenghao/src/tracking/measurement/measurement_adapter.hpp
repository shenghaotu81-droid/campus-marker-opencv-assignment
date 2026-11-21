#pragma once
#include "vision/config/tracking_config.hpp"

namespace mark::tracking::detail
{
    struct PositionMeasurement
    {
        cv::Vec3d position_;
        cv::Matx33d R_;
    };

    int64_t sample_time(const pose::CaptureStamp &);
    std::vector<PositionMeasurement> adapt_measurements(const TrackingFrame &,
                                                        const TrackingConfig &);
}
