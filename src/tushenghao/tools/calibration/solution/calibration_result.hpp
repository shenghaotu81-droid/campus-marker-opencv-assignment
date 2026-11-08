#pragma once

#include <opencv2/opencv.hpp>

#include <string>
#include <vector>

#include "observation/calibration_detection.hpp"

// 相机标定结果
struct CalibrationResult
{
    cv::Size image_size_;
    std::string source_path_;
    std::vector<int> frame_ids_;

    // 相机内参矩阵
    cv::Mat camera_matrix_;

    // 畸变参数
    cv::Mat dist_coeffs_;

    // 重投影误差
    double reprojection_error_ = 0.0;
};

// 执行相机标定
bool run_camera_calibration(const CalibrationVideoInfo &, const CalibrationDetectionResult &,
                            CalibrationResult &);

// 保存标定结果到 YAML
bool save_calibration_result(const CalibrationConfig &config, const CalibrationResult &result);
