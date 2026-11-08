#pragma once

#include <opencv2/opencv.hpp>

#include <vector>

#include "input/calibration_config.hpp"

// 标定视频基本信息
struct CalibrationVideoInfo
{
    std::string source_path_;

    // 视频宽度
    int width_ = 0;

    // 视频高度
    int height_ = 0;

    // 视频总帧数
    int frame_count_ = 0;
};

// 打开视频并读取基本信息
bool open_calibration_video(const CalibrationConfig &config, cv::VideoCapture &capture,
                            CalibrationVideoInfo &info);

// 抽帧保留源身份；后续检测失败也不丢掉对应帧号。
struct SampledFrame { int frame_id_{}; cv::Mat image_; };
// 根据帧编号列表抽取图像
// frame_list 为空时不执行抽帧
bool extract_frames_by_list(cv::VideoCapture &capture, const std::vector<int> &frame_list_,
                            std::vector<SampledFrame> &frames_);
