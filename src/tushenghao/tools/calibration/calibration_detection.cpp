/* 职责：
• 接收 frames
• 对每张图做 circles grid 检测
• 成功帧加入 image_points
• 根据配置生成对应 object_points
*/
/* 把"图像"变成"标定用的点对应关系"：
1. 先按板子参数（7x7、0.03m）生成一套 3D 世界坐标点
2. 逐张处理你抽出来的帧：转灰度 → findCirclesGrid 找圆点(输入一张灰度图 + 标定板板尺寸（7x7），输出 49 个圆心的像素坐标；跟 findChessboardCorners（找棋盘格角点）类似，一个找圆点一个找方格角点)
3. 找到了：把 2D 像素坐标存进 image_points，对应的 3D 点存进 object_points
4. 没找到：跳过这帧（continue），不报错
5. 全部帧都没找到：返回 false，报"未检测到有效标定板"
6. 输出的 image_points + object_points 就是下一步 calibrateCamera 的输入。
*/
#include "calibration_detection.hpp"

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
bool detect_calibration_points(const CalibrationConfig &config, const std::vector<cv::Mat> &frames,
                               CalibrationDetectionResult &result)
{
    // 生成一次标定板世界坐标
    std::vector<cv::Point3f> object_points = create_object_points(config);

    for (const auto &frame : frames)
    {
        cv::Mat gray;

        // 转换为灰度图
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

        std::vector<cv::Point2f> corners;

        cv::Size board_size(config.board_cols, config.board_rows);

        // 检测圆点阵
        bool found = cv::findCirclesGrid(gray, board_size, corners, cv::CALIB_CB_SYMMETRIC_GRID);

        if (!found)
        {
            // 当前帧检测失败，跳过(标定视频里部分帧失败是正常情况)
            continue;
        }

        result.image_points.push_back(corners);

        result.object_points.push_back(object_points);
    }

    if (result.image_points.empty())
    {
        std::cerr << "未检测到有效标定板" << std::endl;

        return false;
    }

    return true;
}