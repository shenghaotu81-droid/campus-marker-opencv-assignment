#include "calibration_video.hpp"

#include <iostream>

// 打开视频并读取基本信息
bool open_calibration_video(const CalibrationConfig &config, cv::VideoCapture &capture,
                            CalibrationVideoInfo &info)
{
    capture.open(config.video_path);

    if (!capture.isOpened())
    {
        std::cerr << "无法打开标定视频: " << config.video_path << std::endl;

        return false;
    }

    info.width = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));

    info.height = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));

    info.frame_count = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_COUNT));

    return true;
}

// 根据帧编号列表抽取图像
bool extract_frames_by_list(cv::VideoCapture &capture, const std::vector<int> &frame_list,
                            std::vector<cv::Mat> &frames)
{
    // 没有指定帧时直接跳过
    if (frame_list.empty())
    {
        return true;
    }

    for (int frame_index : frame_list)
    {
        capture.set(cv::CAP_PROP_POS_FRAMES, frame_index);

        cv::Mat frame;

        if (!capture.read(frame))
        {
            std::cerr << "读取帧失败: " << frame_index << std::endl;

            return false;
        }

        frames.push_back(frame);
    }

    return true;
}