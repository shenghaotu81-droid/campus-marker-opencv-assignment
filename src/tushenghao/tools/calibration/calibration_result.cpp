#include "calibration_result.hpp"

#include <iostream>
#include <cmath>

// 计算平均重投影误差
static double
calculate_reprojection_error(const std::vector<std::vector<cv::Point3f>> &object_points,
                             const std::vector<std::vector<cv::Point2f>> &image_points,
                             const std::vector<cv::Mat> &rvecs, const std::vector<cv::Mat> &tvecs,
                             const cv::Mat &camera_matrix, const cv::Mat &dist_coeffs)
{
    double total_error = 0.0;
    int total_points = 0;

    for (size_t i = 0; i < object_points.size(); ++i)
    {
        std::vector<cv::Point2f> projected_points;

        cv::projectPoints(object_points[i], rvecs[i], tvecs[i], camera_matrix, dist_coeffs,
                          projected_points);

        double error = cv::norm(image_points[i], projected_points, cv::NORM_L2);

        total_error += error * error;  // 先累加平方，最后统一开根号
        total_points += static_cast<int>(object_points[i].size());
    }

    if (total_points == 0)
    {
        return -1.0;
    }

    return std::sqrt(total_error / total_points);
}

// 执行相机标定
bool run_camera_calibration(const CalibrationVideoInfo &video, const CalibrationDetectionResult &observed,
                            CalibrationResult &result)
{
    // C05：求解只接收成功点对；来源映射与求解数组按同一显式顺序建立。
    std::vector<std::vector<cv::Point3f>> object_points;
    std::vector<std::vector<cv::Point2f>> image_points;
    result.frame_ids.clear();
    for (const auto &observation : observed.observations)
        if (observation.detected)
        {
            image_points.push_back(observation.points);
            object_points.push_back(observed.object_points);
            result.frame_ids.push_back(observation.frame_id);
        }
    result.image_size = {video.width, video.height};
    result.source_path = video.source_path;
    if (object_points.empty() || image_points.empty())
    {
        std::cerr << "没有有效标定数据" << std::endl;

        return false;
    }

    const auto image_size = result.image_size;

    std::vector<cv::Mat> rvecs;
    std::vector<cv::Mat> tvecs;

    // C05：保留原求解和独立重投影复核，不共用求解器的 RMS 代替检查。
    double cv_rms = cv::calibrateCamera(object_points, image_points, image_size,
                                        result.camera_matrix, result.dist_coeffs, rvecs, tvecs);

    result.reprojection_error = calculate_reprojection_error(
        object_points, image_points, rvecs, tvecs, result.camera_matrix, result.dist_coeffs);

    // 检验修复成果
    std::cout << "手算 RMSE: " << result.reprojection_error << ", OpenCV RMS: " << cv_rms
              << std::endl;

    return true;
}

// 保存标定结果
bool save_calibration_result(const CalibrationConfig &config, const CalibrationResult &result)
{
    cv::FileStorage fs(config.output_path, cv::FileStorage::WRITE);

    if (!fs.isOpened())
    {
        std::cerr << "无法保存标定结果: " << config.output_path << std::endl;

        return false;
    }

    fs << "schema_version" << 1;

    fs << "camera_matrix" << result.camera_matrix;

    fs << "distortion_coefficients" << result.dist_coeffs;

    fs << "image_width" << result.image_size.width;

    fs << "image_height" << result.image_size.height;

    fs << "reprojection_error" << result.reprojection_error;

    return true;
}