#pragma once
#include "vision/types/tracking_types.hpp"
namespace mark::tracking
{
    void write_tracking_result(cv::FileStorage &, const TrackingResult &);
}
