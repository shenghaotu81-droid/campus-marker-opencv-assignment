#include "observation/calibration_detection.hpp"

#include <iostream>

// 根据标定板参数生成3D世界坐标点(static 表示它只在这个文件里用，不对外暴露)
static std::vector<cv::Point3f> create_object_points(const CalibrationConfig &config)
{
    std::vector<cv::Point3f> object_points_;

    for (int row = 0; row < config.board_rows_; ++row)
    {
        for (int col = 0; col < config.board_cols_; ++col)
        {
            object_points_.emplace_back(static_cast<float>(col * config.point_spacing_),

                                       static_cast<float>(row * config.point_spacing_),

                                       0.0f);
        }
    }

    return object_points_;
}

// 在输入帧中检测圆点阵(主函数)
bool detect_calibration_points(const CalibrationConfig &config, const std::vector<SampledFrame> &frames_,
                               CalibrationDetectionResult &result)
{
    // 生成一次标定板世界坐标
    result.object_points_ = create_object_points(config);
    bool any = false;

    for (const auto &frame : frames_)
    {
        cv::Mat gray;

        // 转换为灰度图
        cv::cvtColor(frame.image_, gray, cv::COLOR_BGR2GRAY);

        std::vector<cv::Point2f> corners_;

        cv::Size board_size(config.board_cols_, config.board_rows_);

        // 检测圆点阵
        bool found = cv::findCirclesGrid(gray, board_size, corners_, cv::CALIB_CB_SYMMETRIC_GRID);

        result.observations_.push_back({frame.frame_id_, std::move(corners_), found});
        any = any || found;
        if (!found) std::cout << "标定板未检出: frame_id=" << frame.frame_id_ << '\n';
    }

    if (!any)
    {
        std::cerr << "未检测到有效标定板" << std::endl;

        return false;
    }

    return true;
}
