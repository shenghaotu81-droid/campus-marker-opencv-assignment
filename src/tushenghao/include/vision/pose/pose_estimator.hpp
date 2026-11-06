#pragma once
#include "vision/types/detector_types.hpp"
#include "vision/config/pose_config.hpp"
#include "vision/types/pose_types.hpp"
namespace mark::pose
{
    class PoseEstimator
    {
public:
        explicit PoseEstimator(G2Config);
        PoseObservation estimate_mark(const FrameResult &, size_t, cv::Size, const CaptureStamp &) const;
        PoseObservation estimate_planar(const std::array<cv::Point3d, 4> &, const std::array<cv::Point2d, 4> &,
                                        cv::Size, const CaptureStamp &, const std::string &) const;
private: G2Config config_;
    };
}
