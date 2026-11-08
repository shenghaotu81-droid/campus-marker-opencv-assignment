#pragma once
#include "frames/calibration_video.hpp"

// 每个抽样帧对应一条观察；未检出只记信息，不伪造点对。
struct BoardObservation {
 int frame_id_{};
 std::vector<cv::Point2f> points_;
 bool detected_{false};
};
struct CalibrationDetectionResult {
 std::vector<cv::Point3f> object_points_;
 std::vector<BoardObservation> observations_;
};
bool detect_calibration_points(const CalibrationConfig &, const std::vector<SampledFrame> &,
                               CalibrationDetectionResult &);
