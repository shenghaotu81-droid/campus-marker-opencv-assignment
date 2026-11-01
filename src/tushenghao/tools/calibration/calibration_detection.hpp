#pragma once
#include "calibration_video.hpp"

// C04：每个抽样帧对应一条观察；未检出只记信息，不伪造点对。
struct BoardObservation {
 int frame_id{};
 std::vector<cv::Point2f> points;
 bool detected{false};
};
struct CalibrationDetectionResult {
 std::vector<cv::Point3f> object_points;
 std::vector<BoardObservation> observations;
};
bool detect_calibration_points(const CalibrationConfig &, const std::vector<SampledFrame> &,
                               CalibrationDetectionResult &);
