/* 实现：
• cv::calibrateCamera
• reprojection error
• FileStorage 输出
*/
/* 算出 K 和畸变并存盘:
1. run_camera_calibration（主函数）:
输入：image_points + object_points（Step 5 产出的 2D-3D 对应）
调 cv::calibrateCamera() 求解，得到 camera_matrix、dist_coeffs，以及每张图的外参 rvecs/tvecs
再调下面的误差计算函数，算出重投影误差
两个结果都存进 CalibrationResult 结构体
2. calculate_reprojection_error（static 辅助函数）:
对每张标定图：用刚算出的 K/dist + 外参，把 3D 点重新投影回 2D（cv::projectPoints）
比较"投影回去的 2D 点" vs "实际检测到的 2D 点"，算偏差
所有点取平均，就是重投影误差——越小说明标定越准
static 表示只在这个文件里用
3. save_calibration_result:
用 cv::FileStorage 写 yaml
按 acceptance 要求存：schema_version、camera_matrix、distortion_coefficients、image_width、image_height，外加一个 reprojection_error 方便你看标定质量
输出路径是 config.output_path（默认 __FILE__ 推导的 config/calibration.yaml）
*/
#include "calibration_result.hpp"

#include <iostream>

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

        total_error += error;
        total_points += static_cast<int>(object_points[i].size());
    }

    if (total_points == 0)
    {
        return -1.0;
    }

    return total_error / total_points;
}

// 执行相机标定
bool run_camera_calibration(const CalibrationConfig &config,
                            const std::vector<std::vector<cv::Point3f>> &object_points,
                            const std::vector<std::vector<cv::Point2f>> &image_points,
                            CalibrationResult &result)
{
    if (object_points.empty() || image_points.empty())
    {
        std::cerr << "没有有效标定数据" << std::endl;

        return false;
    }

    cv::Size image_size(config.image_width, config.image_height);

    std::vector<cv::Mat> rvecs;
    std::vector<cv::Mat> tvecs;

    // 非线性优化求解
    cv::calibrateCamera(object_points, image_points, image_size, result.camera_matrix,
                        result.dist_coeffs, rvecs, tvecs);

    result.reprojection_error = calculate_reprojection_error(
        object_points, image_points, rvecs, tvecs, result.camera_matrix, result.dist_coeffs);

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

    fs << "image_width" << config.image_width;

    fs << "image_height" << config.image_height;

    fs << "reprojection_error" << result.reprojection_error;

    return true;
}