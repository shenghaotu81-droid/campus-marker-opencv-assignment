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
    std::string video_path_;

    // 标定结果YAML输出路径
    std::string output_path_;

    // 标定板圆点数量(尺寸)
    int board_rows_ = 0;
    int board_cols_ = 0;

    // 相邻圆点之间的实际距离，单位：米
    double point_spacing_ = 0.0;

    // 指定抽取的视频帧编号
    // 为空时表示不进行指定抽帧
    std::vector<int> frame_list_;
};

// 读取 YAML 中显式填写的字段，覆盖默认值。
bool load_config_yaml(const std::string &config_path, CalibrationConfig &config);

// 初始化标定工具默认配置
void initialize_default_config(CalibrationConfig &config);

// 检查完整 CLI 后提取 --config；未知/重复/缺值参数抛异常，未给配置则返回 false。
bool extract_config_path(int argc, char **argv, std::string &config_path);

// 按 CLI 覆盖 YAML；数值须完整解析，错误由入口报告后终止。
void apply_cli_overrides(int argc, char **argv, CalibrationConfig &config);
