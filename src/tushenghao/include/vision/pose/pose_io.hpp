#pragma once
#include "vision/types/pose_types.hpp"
namespace mark::pose
{
    void write_capture_stamp(cv::FileStorage &, const CaptureStamp &);
    void write_pose_observation(cv::FileStorage &, const PoseObservation &);
}
