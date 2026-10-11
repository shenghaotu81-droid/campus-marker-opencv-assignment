/* 全流程：
yaml
 ↓
config
 ↓
video
 ↓
frame_list
 ↓
findCirclesGrid
 ↓
image_points/object_points
 ↓
calibrateCamera
 ↓
calibration.yaml
*/
#include <opencv2/opencv.hpp>

#include "calibration_config.hpp"
#include "calibration_video.hpp"
#include "calibration_detection.hpp"
#include "calibration_result.hpp"

#include <iostream>
#include <string>

int main(int argc, char **argv)
{
    CalibrationConfig config;

    // 初始化默认配置
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

    cv::VideoCapture capture;

    CalibrationVideoInfo video_info;

    // 打开视频
    if (!open_calibration_video(config, capture, video_info))
    {
        return -1;
    }

    config.image_width = video_info.width;

    config.image_height = video_info.height;

    std::vector<cv::Mat> frames;

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
    std::cout << "检测成功帧数量: " << detection_result.image_points.size() << std::endl;

    CalibrationResult calibration_result;

    // 执行相机标定
    if (!run_camera_calibration(config, detection_result.object_points,
                                detection_result.image_points, calibration_result))
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

    std::cout << "图像尺寸: " << config.image_width << " x " << config.image_height << std::endl;

    std::cout << "重投影误差: " << calibration_result.reprojection_error << std::endl;

    std::cout << "输出文件: " << config.output_path << std::endl;

    return 0;
}