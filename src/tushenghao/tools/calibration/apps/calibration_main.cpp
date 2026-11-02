#include <opencv2/opencv.hpp>

#include "input/calibration_config.hpp"
#include "frames/calibration_video.hpp"
#include "observation/calibration_detection.hpp"
#include "results/calibration_result.hpp"

#include <iostream>
#include <algorithm>
#include <string>

int main(int argc, char **argv)
{
    CalibrationConfig config;

    // C01：默认值→严格 YAML→合法 CLI 覆盖；失败不打开视频、不写标定结果。
    try
    {
        initialize_default_config(config);

        std::string config_path;

        // 获取 YAML 配置文件路径
        if (!extract_config_path(argc, argv, config_path))
        {
            std::cerr << "缺少 --config 参数" << std::endl;

            return -1;
        }

        // 加载 YAML 配置
        if (!load_config_yaml(config_path, config))
        {
            return -1;
        }

        // 使用命令行参数CLI覆盖 YAML 配置
        apply_cli_overrides(argc, argv, config);

        // 检查最终配置是否合法
        if (!validate_config(config))
        {
            return -1;
        }
    }
    catch (const std::exception &error)
    {
        std::cerr << "标定配置错误: " << error.what() << '\n';
        return -1;
    }

    cv::VideoCapture capture;

    CalibrationVideoInfo video_info;

    // 打开视频
    if (!open_calibration_video(config, capture, video_info))
    {
        return -1;
    }

    std::vector<SampledFrame> frames;

    // 根据配置抽帧
    if (!extract_frames_by_list(capture, config.frame_list, frames))
    {
        return -1;
    }

    CalibrationDetectionResult detection_result;

    // 检测标定板圆点
    if (!detect_calibration_points(config, frames, detection_result))
    {
        return -1;
    }

    // 调试信息：多少帧检测到了圆点
    std::cout << "检测成功帧数量: " << std::count_if(detection_result.observations.begin(), detection_result.observations.end(), [](const auto &o) { return o.detected; }) << std::endl;

    CalibrationResult calibration_result;

    // 执行相机标定
    if (!run_camera_calibration(video_info, detection_result, calibration_result))
    {
        return -1;
    }

    // 保存标定结果
    if (!save_calibration_result(config, calibration_result))
    {
        return -1;
    }

    // 输出摘要
    std::cout << "标定完成" << std::endl;

    std::cout << "图像尺寸: " << calibration_result.image_size.width << " x " << calibration_result.image_size.height << std::endl;

    std::cout << "重投影误差: " << calibration_result.reprojection_error << std::endl;

    std::cout << "输出文件: " << config.output_path << std::endl;

    return 0;
}
