#include "observation/calibration_detection.hpp"

#include <iostream>

// 根据标定板参数生成3D世界坐标点(static 表示它只在这个文件里用，不对外暴露)
static std::vector<cv::Point3f> create_object_points(const CalibrationConfig &config)
{
    std::vector<cv::Point3f> object_points;

    for (int row = 0; row < config.board_rows; ++row)
    {
        for (int col = 0; col < config.board_cols; ++col)
        {
            object_points.emplace_back(static_cast<float>(col * config.point_spacing),

                                       static_cast<float>(row * config.point_spacing),

                                       0.0f);
        }
    }

    return object_points;
}

// 在输入帧中检测圆点阵(主函数)
bool detect_calibration_points(const CalibrationConfig &config, const std::vector<SampledFrame> &frames,
                               CalibrationDetectionResult &result)
{
    // 生成一次标定板世界坐标
    result.object_points = create_object_points(config);
    bool any = false;

    for (const auto &frame : frames)
    {
        cv::Mat gray;

        // 转换为灰度图
        cv::cvtColor(frame.image, gray, cv::COLOR_BGR2GRAY);

        std::vector<cv::Point2f> corners;

        cv::Size board_size(config.board_cols, config.board_rows);

        // 检测圆点阵
        bool found = cv::findCirclesGrid(gray, board_size, corners, cv::CALIB_CB_SYMMETRIC_GRID);

        result.observations.push_back({frame.frame_id, std::move(corners), found});
        any = any || found;
        if (!found) std::cout << "标定板未检出: frame_id=" << frame.frame_id << '\n';
    }

    if (!any)
    {
        std::cerr << "未检测到有效标定板" << std::endl;

        return false;
    }

    return true;
}