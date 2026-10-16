// AprilTag PImpl与退出适配
#pragma once

#include <memory>
#include "mark/pose_estimator.hpp"

namespace mark::pose
{
// 官方 AprilTag 对象由 PImpl 独占；避免 C 库类型污染公开头、避免跨实例共享状态。（互不干扰）
// PImpl 封装官方 AprilTag C 库（Impl 藏了 C 类型）。detect 输入 BGR 图，输出 TagObservation 列表
class TagDetector
{
public:
    explicit TagDetector(TagConfig config);
    ~TagDetector();
    TagDetector(const TagDetector &) = delete;
    TagDetector &operator=(const TagDetector &) = delete;

    std::vector<TagObservation> detect(const cv::Mat &bgr,
                                       const CaptureStamp &stamp_);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// 未知尺寸仍能输出像素 Tag，但不能构造公制对准测量。
// 把单个 Tag 转成退出位姿 ExitObservation，内部调 PoseEstimator::estimate_planar（通用 PnP）
ExitObservation estimate_exit(const TagObservation &tag_, cv::Size original_size,
                              const PoseEstimator &estimator);
} // namespace mark::pose