#include "solution/calibration_result.hpp"

#include <iostream>
#include <cmath>

// 计算平均重投影误差
static double
calculate_reprojection_error(const std::vector<std::vector<cv::Point3f>> &object_points_,
                             const std::vector<std::vector<cv::Point2f>> &image_points,
                             const std::vector<cv::Mat> &rvecs, const std::vector<cv::Mat> &tvecs,
                             const cv::Mat &camera_matrix_, const cv::Mat &dist_coeffs_)
{
    double total_error = 0.0;
    int total_points = 0;

    for (size_t i = 0; i < object_points_.size(); ++i)
    {
        std::vector<cv::Point2f> projected_points;

        cv::projectPoints(object_points_[i], rvecs[i], tvecs[i], camera_matrix_, dist_coeffs_,
                          projected_points);

        double error = cv::norm(image_points[i], projected_points, cv::NORM_L2);

        total_error += error * error;  // 先累加平方，最后统一开根号
        total_points += static_cast<int>(object_points_[i].size());
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
    // 求解只接收成功点对；来源映射与求解数组按同一显式顺序建立。
    std::vector<std::vector<cv::Point3f>> object_points_;
    std::vector<std::vector<cv::Point2f>> image_points;
    result.frame_ids_.clear();
    for (const auto &observation : observed.observations_)
        if (observation.detected_)
        {
            image_points.push_back(observation.points_);
            object_points_.push_back(observed.object_points_);
            result.frame_ids_.push_back(observation.frame_id_);
        }
    result.image_size_ = {video.width_, video.height_};
    result.source_path_ = video.source_path_;
    if (object_points_.empty() || image_points.empty())
    {
        std::cerr << "没有有效标定数据" << std::endl;

        return false;
    }

    const auto image_size_ = result.image_size_;

    std::vector<cv::Mat> rvecs;
    std::vector<cv::Mat> tvecs;

    // 执行相机求解和独立重投影复核，不共用求解器的 RMS 代替检查。
    double cv_rms = cv::calibrateCamera(object_points_, image_points, image_size_,
                                        result.camera_matrix_, result.dist_coeffs_, rvecs, tvecs);

    result.reprojection_error_ = calculate_reprojection_error(
        object_points_, image_points, rvecs, tvecs, result.camera_matrix_, result.dist_coeffs_);

    // 打印独立计算与求解器返回的重投影误差，供标定结果核对。
    std::cout << "手算 RMSE: " << result.reprojection_error_ << ", OpenCV RMS: " << cv_rms
              << std::endl;

    return true;
}

// 保存标定结果
bool save_calibration_result(const CalibrationConfig &config, const CalibrationResult &result)
{
    cv::FileStorage fs(config.output_path_, cv::FileStorage::WRITE);

    if (!fs.isOpened())
    {
        std::cerr << "无法保存标定结果: " << config.output_path_ << std::endl;

        return false;
    }

    fs << "camera_matrix" << result.camera_matrix_;

    fs << "distortion_coefficients" << result.dist_coeffs_;

    fs << "image_width" << result.image_size_.width;

    fs << "image_height" << result.image_size_.height;

    fs << "reprojection_error" << result.reprojection_error_;

    return true;
}
