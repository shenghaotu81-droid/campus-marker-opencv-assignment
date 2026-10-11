// 标定帧检测（circles grid）
/*
frame
 ↓
gray
 ↓
findCirclesGrid()
 ↓ success
image_points
 ↓
generate object_points
 ↓
object_points
*/
#pragma once

#include <opencv2/opencv.hpp>

#include <vector>

#include "calibration_config.hpp"

// 标定检测结果
struct CalibrationDetectionResult
{
    // 成功检测的图像点
    std::vector<std::vector<cv::Point2f>> image_points;

    // 对应的世界坐标点
    std::vector<std::vector<cv::Point3f>> object_points;
};

// 在输入帧中检测圆点阵
bool detect_calibration_points(const CalibrationConfig &config, const std::vector<cv::Mat> &frames,
                               CalibrationDetectionResult &result);