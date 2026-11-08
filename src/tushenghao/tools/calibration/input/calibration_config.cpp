#include "input/calibration_config.hpp"

#include <opencv2/core.hpp>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

namespace
{
    // 配置路径提取和参数覆盖共用 CLI 协议；先检查完整命令，再读取 YAML 或覆盖配置。
    std::map<std::string, std::string> parse_cli(int argc, char **argv)
    {
        const std::set<std::string> allowed{"--config", "--video", "--output", "--rows",
                                            "--cols", "--spacing"};
        std::map<std::string, std::string> values;
        for (int i = 1; i < argc; ++i)
        {
            const std::string key = argv[i];
            if (!allowed.count(key))
                throw std::invalid_argument("未知标定参数: " + key);
            if (i + 1 == argc || std::string(argv[i + 1]).rfind("--", 0) == 0)
                throw std::invalid_argument("标定参数缺值: " + key);
            if (!values.emplace(key, argv[++i]).second)
                throw std::invalid_argument("重复标定参数: " + key);
        }
        return values;
    }

    int cli_integer(const std::string &value)
    {
        size_t used = 0;
        const int result = std::stoi(value, &used);
        if (used != value.size())
            throw std::invalid_argument("整数含多余字符: " + value);
        return result;
    }

}

// 默认值步骤：输入空配置，填入板参数和默认输出路径，供 YAML/CLI 覆盖。
void initialize_default_config(CalibrationConfig &config)
{
    config.board_rows_ = 7;
    config.board_cols_ = 7;
    config.point_spacing_ = 0.03;
    const std::filesystem::path source_path_(VISION_SOURCE_ROOT);
    config.output_path_ =
        (source_path_ / "config" / "calibration.yaml").string();
}

// YAML 为手填参数，只读取显式字段，其余保留默认值供 CLI 覆盖。
bool load_config_yaml(const std::string &config_path, CalibrationConfig &config)
{
    cv::FileStorage fs(config_path, cv::FileStorage::READ);
    if (!fs.isOpened()) return false;
    if (!fs["video_path"].empty()) config.video_path_ = static_cast<std::string>(fs["video_path"]);
    if (!fs["output_path"].empty()) config.output_path_ = static_cast<std::string>(fs["output_path"]);
    if (!fs["board_rows"].empty()) config.board_rows_ = static_cast<int>(fs["board_rows"]);
    if (!fs["board_cols"].empty()) config.board_cols_ = static_cast<int>(fs["board_cols"]);
    if (!fs["point_spacing"].empty()) config.point_spacing_ = static_cast<double>(fs["point_spacing"]);
    for (const auto &node : fs["frame_list"])
        config.frame_list_.push_back(static_cast<int>(node));
    return true;
}

// 提取配置路径前验证所有参数，首个 --config 不得掩盖其后的非法参数。
bool extract_config_path(int argc, char **argv, std::string &config_path)
{
    const auto values = parse_cli(argc, argv);
    const auto found = values.find("--config");
    if (found == values.end())
        return false;
    config_path = found->second;
    return true;
}

// CLI 步骤：只覆盖显式字段；整数/实数须消费完整 token，错误由入口终止。
void apply_cli_overrides(int argc, char **argv, CalibrationConfig &config)
{
    for (const auto &item : parse_cli(argc, argv))
    {
        const auto &key = item.first;
        const auto &value = item.second;
        try
        {
            if (key == "--video") config.video_path_ = value;
            else if (key == "--output") config.output_path_ = value;
            else if (key == "--rows" || key == "--cols")
            {
                const auto count = cli_integer(value);
                if (count <= 0) throw std::invalid_argument("标定板尺寸须为正整数");
                if (key == "--rows") config.board_rows_ = count;
                else config.board_cols_ = count;
            }
            else if (key == "--spacing")
            {
                size_t used = 0;
                config.point_spacing_ = std::stod(value, &used);
                if (used != value.size() || !std::isfinite(config.point_spacing_) || config.point_spacing_ <= 0)
                    throw std::invalid_argument("间距须为完整的有限数");
            }
        }
        catch (const std::exception &error)
        {
            throw std::invalid_argument(key + ": " + error.what());
        }
    }
}
