#pragma once

#include <opencv2/opencv.hpp>

#include <vector>

#include "input/calibration_config.hpp"

// 标定视频基本信息
struct CalibrationVideoInfo
{
    std::string source_path;

    // 视频宽度
    int width = 0;

    // 视频高度
    int height = 0;

    // 视频总帧数
    int frame_count = 0;
};

// 打开视频并读取基本信息
bool open_calibration_video(const CalibrationConfig &config, cv::VideoCapture &capture,
                            CalibrationVideoInfo &info);

// C03：抽帧保留源身份；后续检测失败也不丢掉对应帧号。
struct SampledFrame { int frame_id{}; cv::Mat image; };
// 根据帧编号列表抽取图像
// frame_list 为空时不执行抽帧
bool extract_frames_by_list(cv::VideoCapture &capture, const std::vector<int> &frame_list,
                            std::vector<SampledFrame> &frames);