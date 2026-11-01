#include "calibration_config.hpp"

#include <opencv2/core.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>

namespace
{
    constexpr int kSupportedSchemaVersion = 1;

    // C01：FileStorage 4.x 会先把越界整数窄化（如 4294967297→1），节点检查已太晚。
    // 只复核整数配置/帧列表的原始 token；类型与 YAML 语法仍由 FileStorage 验证。
    void validate_original_integers(const std::string &source)
    {
        auto text = source;
        const std::regex literals(R"lit("(?:\\.|[^"\\])*"|'(?:''|[^'])*'|#[^\n]*)lit");
        for (std::sregex_iterator it(source.begin(), source.end(), literals), end; it != end; ++it)
        {
            size_t after = size_t(it->position() + it->length());
            while (after < source.size() && (source[after] == ' ' || source[after] == '\t')) ++after;
            if ((*it).str()[0] != '#' && after < source.size() && source[after] == ':') continue;
            for (size_t i = size_t(it->position()); i < size_t(it->position() + it->length()); ++i)
                if (text[i] != '\n' && text[i] != '\r') text[i] = ' ';
        }
        const std::regex token(R"((^|[\s,\[])([+-]?(?:0[xX][0-9a-fA-F]+|[0-9]+))(?=[\s,\]]|$))");
        auto require_range = [&](const std::string &values)
        {
            for (std::sregex_iterator it(values.begin(), values.end(), token), end; it != end; ++it)
            {
                const auto value = (*it)[2].str();
                size_t used = 0;
                const auto number = std::stoll(value, &used, 0);
                if (used != value.size() || number < std::numeric_limits<int>::min() ||
                    number > std::numeric_limits<int>::max())
                    throw std::invalid_argument("标定整数超出可表示范围: " + value);
            }
        };
        const std::regex fields(R"((?:^|[\n{,])[ \t]*['"]?(?:schema_version|board_rows|board_cols|frame_list)['"]?[ \t]*:[ \t]*(\[[^\]]*\]|[^\n,}]*))");
        for (std::sregex_iterator it(text.begin(), text.end(), fields), end; it != end; ++it)
            require_range((*it)[1].str());
        const std::regex block_frames(R"((?:^|\n)[ \t]*['"]?frame_list['"]?[ \t]*:[ \t]*\r?\n((?:[ \t]+-[^\n]*(?:\n|$))+))");
        for (std::sregex_iterator it(text.begin(), text.end(), block_frames), end; it != end; ++it)
            require_range((*it)[1].str());
    }

    // C01：两个既有入口共用 CLI 协议；先检查完整命令，再读取 YAML 或覆盖配置。
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

    int yaml_integer(const cv::FileNode &node)
    {
        if (!node.isInt())
            throw std::invalid_argument("标定字段须为整数: " + node.name());
        return static_cast<int>(node);
    }

    std::string yaml_text(const cv::FileNode &node)
    {
        if (!node.isString())
            throw std::invalid_argument("标定字段须为字符串: " + node.name());
        return static_cast<std::string>(node);
    }
}

// C01 默认值步骤：输入空配置，填入既有板参数和默认输出路径，供 YAML/CLI 覆盖。
void initialize_default_config(CalibrationConfig &config)
{
    config.board_rows = 7;
    config.board_cols = 7;
    config.point_spacing = 0.03;
    const std::filesystem::path source_path(__FILE__);
    config.output_path =
        (source_path.parent_path().parent_path().parent_path() / "config" / "calibration.yaml").string();
}

// C01 YAML 步骤：严格读取字段；未知/重复键、错误类型或非有限值返回失败。
bool load_config_yaml(const std::string &config_path, CalibrationConfig &config)
{
    try
    {
        std::ifstream input(config_path);
        if (!input) throw std::invalid_argument("无法打开标定配置: " + config_path);
        const std::string source((std::istreambuf_iterator<char>(input)), {});
        if (input.bad()) throw std::invalid_argument("无法读取标定配置: " + config_path);
        validate_original_integers(source);
        cv::FileStorage fs(source, cv::FileStorage::READ | cv::FileStorage::MEMORY);
        if (!fs.isOpened())
            throw std::invalid_argument("无法打开标定配置: " + config_path);
        const std::set<std::string> allowed{"schema_version", "video_path", "output_path",
                                            "board_rows", "board_cols", "point_spacing",
                                            "frame_list"};
        std::set<std::string> seen;
        for (const auto &node : fs.root())
        {
            const auto key = node.name();
            if (!allowed.count(key) || !seen.insert(key).second)
                throw std::invalid_argument("未知或重复标定字段: " + key);
        }
        if (yaml_integer(fs["schema_version"]) != kSupportedSchemaVersion)
            throw std::invalid_argument("缺少或不支持的 schema_version");
        if (seen.count("video_path")) config.video_path = yaml_text(fs["video_path"]);
        if (seen.count("output_path")) config.output_path = yaml_text(fs["output_path"]);
        if (seen.count("board_rows")) config.board_rows = yaml_integer(fs["board_rows"]);
        if (seen.count("board_cols")) config.board_cols = yaml_integer(fs["board_cols"]);
        if (seen.count("point_spacing"))
        {
            const auto node = fs["point_spacing"];
            if (!node.isInt() && !node.isReal())
                throw std::invalid_argument("point_spacing 须为有限数");
            config.point_spacing = node.real();
            if (!std::isfinite(config.point_spacing))
                throw std::invalid_argument("point_spacing 须为有限数");
        }
        const auto frames = fs["frame_list"];
        if (seen.count("frame_list"))
        {
            if (!frames.isSeq())
                throw std::invalid_argument("frame_list 须为整数序列");
            for (const auto &node : frames)
            {
                const int index = yaml_integer(node);
                if (index < 0)
                    throw std::invalid_argument("frame_list 帧号不能为负");
                config.frame_list.push_back(index);
            }
        }
        return true;
    }
    catch (const std::exception &error)
    {
        std::cerr << "标定 YAML 错误: " << error.what() << '\n';
        return false;
    }
}

// C01：提取配置路径前验证所有参数，首个 --config 不得掩盖其后的非法参数。
bool extract_config_path(int argc, char **argv, std::string &config_path)
{
    const auto values = parse_cli(argc, argv);
    const auto found = values.find("--config");
    if (found == values.end())
        return false;
    config_path = found->second;
    return true;
}

// C01 CLI 步骤：只覆盖显式字段；整数/实数须消费完整 token，错误由入口终止。
void apply_cli_overrides(int argc, char **argv, CalibrationConfig &config)
{
    for (const auto &item : parse_cli(argc, argv))
    {
        const auto &key = item.first;
        const auto &value = item.second;
        try
        {
            if (key == "--video") config.video_path = value;
            else if (key == "--output") config.output_path = value;
            else if (key == "--rows") config.board_rows = cli_integer(value);
            else if (key == "--cols") config.board_cols = cli_integer(value);
            else if (key == "--spacing")
            {
                size_t used = 0;
                config.point_spacing = std::stod(value, &used);
                if (used != value.size() || !std::isfinite(config.point_spacing))
                    throw std::invalid_argument("间距须为完整的有限数");
            }
        }
        catch (const std::exception &error)
        {
            throw std::invalid_argument(key + ": " + error.what());
        }
    }
}

// C01→C04：最终配置只拒绝非法输入，不依据标定质量筛选帧或推定硬件资格。
bool validate_config(const CalibrationConfig &config)
{
    if (config.video_path.empty())
    {
        std::cerr << "缺少输入视频路径\n";
        return false;
    }
    if (config.output_path.empty())
    {
        std::cerr << "缺少输出文件路径\n";
        return false;
    }
    if (config.board_rows <= 0 || config.board_cols <= 0)
    {
        std::cerr << "标定板尺寸非法\n";
        return false;
    }
    // NaN 不会命中 <=0，须在创建物点前明确排除，否则会污染后续 OpenCV 求解。
    if (!std::isfinite(config.point_spacing) || config.point_spacing <= 0)
    {
        std::cerr << "标定点间距非法\n";
        return false;
    }
    return true;
}
