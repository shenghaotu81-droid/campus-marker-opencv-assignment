// 相机标定与结果输出
/* 接口：
• 标定结果结构
• 执行 calibration
• 保存 yaml
*/
#pragma once

#include <opencv2/opencv.hpp>

#include <string>
#include <vector>

#include "calibration_config.hpp"

// 相机标定结果
struct CalibrationResult
{
    // 相机内参矩阵
    cv::Mat camera_matrix;

    // 畸变参数
    cv::Mat dist_coeffs;

    // 重投影误差
    double reprojection_error = 0.0;
};

// 执行相机标定
bool run_camera_calibration(const CalibrationConfig &config,
                            const std::vector<std::vector<cv::Point3f>> &object_points,
                            const std::vector<std::vector<cv::Point2f>> &image_points,
                            CalibrationResult &result);

// 保存标定结果到 YAML
bool save_calibration_result(const CalibrationConfig &config, const CalibrationResult &result);