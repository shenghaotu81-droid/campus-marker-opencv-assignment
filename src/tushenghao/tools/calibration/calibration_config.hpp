// 配置层
/*
default
  ↓
yaml
  ↓
CLI override
*/
#pragma once

#include <string>
#include <vector>

// 相机标定工具的输入配置
// 所有参数来自默认值、yaml 配置或命令行覆盖(参数来源优先级：默认值 -> YAML 配置 -> 命令行覆盖)
struct CalibrationConfig
{
    // 输入视频路径
    std::string video_path;

    // 标定结果YAML输出路径
    std::string output_path;

    // 标定板圆点数量(尺寸)
    int board_rows = 0;
    int board_cols = 0;

    // 相邻圆点之间的实际距离，单位：米
    double point_spacing = 0.0;

    // 图像尺寸（可由视频读取后填充）
    int image_width = 0;
    int image_height = 0;

    // 标定帧范围
    // start_frame: 起始帧
    // end_frame: 结束帧，-1 表示直到视频结束
    int start_frame = 0;
    int end_frame = -1;

    // 指定抽取的视频帧编号
    // 为空时表示不进行指定抽帧
    std::vector<int> frame_list;
};

// 从 OpenCV YAML 配置文件读取参数
// YAML 中存在字段时覆盖默认值
bool load_config_yaml(const std::string &config_path, CalibrationConfig &config);

// 初始化标定工具默认配置
void initialize_default_config(CalibrationConfig &config);

// 从命令行中提取配置文件路径
// 在 YAML 加载之前调用
bool extract_config_path(int argc, char **argv, std::string &config_path);

// 使用命令行参数覆盖配置
// 在 YAML 加载之后调用
void apply_cli_overrides(int argc, char **argv, CalibrationConfig &config);

// 检查标定配置是否合法
bool validate_config(const CalibrationConfig &config);