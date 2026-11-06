#pragma once
#include <memory>
#include "vision/pose/pose_estimator.hpp"
namespace mark::pose
{
    class TagDetector
    {
public:
        explicit TagDetector(TagConfig);
        ~TagDetector();
        TagDetector(const TagDetector &) = delete;
        TagDetector& operator=(const TagDetector &) = delete;
        std::vector<TagObservation> detect(const cv::Mat &, const CaptureStamp &);
private: struct Impl;
        std::unique_ptr<Impl> impl_;
    };
    PoseObservation estimate_exit(const TagObservation &, cv::Size, const PoseEstimator &);
}
