#include "calibration_config.hpp"

#include <opencv2/opencv.hpp>

#include <filesystem>
#include <iostream>
#include <vector>

// 当前支持的配置文件版本
constexpr int kSupportedSchemaVersion = 1;

// 初始化默认配置
void initialize_default_config(CalibrationConfig &config)
{
    config.board_rows = 7;
    config.board_cols = 7;

    config.point_spacing = 0.03;

    config.start_frame = 0;
    config.end_frame = -1;

    // 根据当前源文件位置推导默认输出路径
    std::filesystem::path source_path(__FILE__);

    std::filesystem::path output_path =
        source_path.parent_path().parent_path().parent_path() / "config" / "calibration.yaml";

    config.output_path = output_path.string();
}

// 从 OpenCV YAML 文件读取配置
bool load_config_yaml(const std::string &config_path, CalibrationConfig &config)
{
    cv::FileStorage fs(config_path, cv::FileStorage::READ);

    if (!fs.isOpened())
    {
        std::cerr << "无法打开配置文件: " << config_path << std::endl;

        return false;
    }

    // 检查配置文件版本
    cv::FileNode schema_node = fs["schema_version"];

    if (schema_node.empty())
    {
        std::cerr << "配置文件缺少 schema_version" << std::endl;

        return false;
    }

    int schema_version = 0;

    schema_node >> schema_version;

    if (schema_version != kSupportedSchemaVersion)
    {
        std::cerr << "不支持的配置文件版本: " << schema_version << std::endl;

        return false;
    }

    // YAML 存在字段时覆盖默认值
    // 读取视频和输出路径
    if (!fs["video_path"].empty())
    {
        fs["video_path"] >> config.video_path;
    }

    if (!fs["output_path"].empty())
    {
        fs["output_path"] >> config.output_path;
    }

    // 读取标定板参数
    if (!fs["board_rows"].empty())
    {
        fs["board_rows"] >> config.board_rows;
    }

    if (!fs["board_cols"].empty())
    {
        fs["board_cols"] >> config.board_cols;
    }

    if (!fs["point_spacing"].empty())
    {
        fs["point_spacing"] >> config.point_spacing;
    }

    // 读取帧范围
    if (!fs["start_frame"].empty())
    {
        fs["start_frame"] >> config.start_frame;
    }

    if (!fs["end_frame"].empty())
    {
        fs["end_frame"] >> config.end_frame;
    }

    // 读取指定抽帧列表
    cv::FileNode frame_node = fs["frame_list"];

    if (!frame_node.empty())
    {
        for (auto iterator = frame_node.begin(); iterator != frame_node.end(); ++iterator)
        {
            int frame_index = 0;

            *iterator >> frame_index;

            config.frame_list.push_back(frame_index);
        }
    }

    return true;
}

// 从命令行提取 --config
bool extract_config_path(int argc, char **argv, std::string &config_path)
{
    for (int i = 1; i < argc; ++i)
    {
        std::string argument = argv[i];

        if (argument == "--config" && i + 1 < argc)
        {
            config_path = argv[++i];

            return true;
        }
    }

    return false;
}

// 使用命令行覆盖配置
void apply_cli_overrides(int argc, char **argv, CalibrationConfig &config)
{
    for (int i = 1; i < argc; ++i)
    {
        std::string argument = argv[i];

        if (argument == "--video" && i + 1 < argc)
        {
            config.video_path = argv[++i];
        }
        else if (argument == "--output" && i + 1 < argc)
        {
            config.output_path = argv[++i];
        }
        else if (argument == "--rows" && i + 1 < argc)
        {
            config.board_rows = std::stoi(argv[++i]);
        }
        else if (argument == "--cols" && i + 1 < argc)
        {
            config.board_cols = std::stoi(argv[++i]);
        }
        else if (argument == "--spacing" && i + 1 < argc)
        {
            config.point_spacing = std::stod(argv[++i]);
        }
        else if (argument == "--start-frame" && i + 1 < argc)
        {
            config.start_frame = std::stoi(argv[++i]);
        }
        else if (argument == "--end-frame" && i + 1 < argc)
        {
            config.end_frame = std::stoi(argv[++i]);
        }
    }
}

// 检查标定配置是否合法
bool validate_config(const CalibrationConfig &config)
{
    // 检查输入视频路径
    if (config.video_path.empty())
    {
        std::cerr << "缺少输入视频路径" << std::endl;

        return false;
    }

    // 检查输出路径
    if (config.output_path.empty())
    {
        std::cerr << "缺少输出文件路径" << std::endl;

        return false;
    }

    // 检查标定板参数
    if (config.board_rows <= 0 || config.board_cols <= 0)
    {
        std::cerr << "标定板尺寸非法" << std::endl;

        return false;
    }

    // 检查圆点间距
    if (config.point_spacing <= 0)
    {
        std::cerr << "标定点间距非法" << std::endl;

        return false;
    }

    // 检查帧范围
    if (config.start_frame < 0)
    {
        std::cerr << "起始帧不能小于 0" << std::endl;

        return false;
    }

    if (config.end_frame != -1 && config.end_frame < config.start_frame)
    {
        std::cerr << "结束帧不能小于起始帧" << std::endl;

        return false;
    }

    return true;
}