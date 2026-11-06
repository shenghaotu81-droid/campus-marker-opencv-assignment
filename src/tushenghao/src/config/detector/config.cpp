// Q01→Q03→Q06：严格读取、分模块校验、导出生效配置；预算值仅来自输入。
#include "detector/config.hpp"
#include <algorithm>
#include "reader/value_reader.hpp"
#include "detector/corner_budget_validation.hpp"
#include <set>
#include <limits>

#include <opencv2/core.hpp>

#include "reader/config_error.hpp"

#include <filesystem>

namespace mark
{

    namespace
    { // 配置错误：文件=<path>，字段=<field>，原因=<reason>

        // 统一生成配置错误，保证错误信息包含文件路径和字段路径。
        [[noreturn]] void throwConfigError(const std::filesystem::path &file,
                                           const std::string &field, const std::string &message)
        {
            throw ConfigError("Config error: file=" + file.string() + ", field=" + field +
                              ", reason=" + message);
        }

        // Q02：Detector 只接收真实数值节点，字符串和 bool 的原严格契约不放松。
        bool readBool(const cv::FileNode &node, const std::filesystem::path &file,
                      const std::string &field)
        {
            return config_value::boolean(node,
                                         [&](auto)
                                         {
                                             throwConfigError(file, field, "expected integer 0/1");
                                         });
        }

        int readInt(const cv::FileNode &node, const std::filesystem::path &file,
                    const std::string &field)
        {
            return config_value::integer(node,
                                         [&](auto)
                                         {
                                             throwConfigError(file, field, "expected integer");
                                         });
        }

        std::string readString(const cv::FileNode &node, const std::filesystem::path &file,
                               const std::string &field)
        {
            return config_value::text(node,
                                      [&](auto)
                                      {
                                          throwConfigError(file, field, "expected string");
                                      });
        }

        double readDouble(const cv::FileNode &node, const std::filesystem::path &file,
                          const std::string &field)
        {
            return config_value::number(node, config_value::NumericPolicy::NodeNumber,
                                        [&](auto error)
                                        {
                                            throwConfigError(file, field,
                                                             error == config_value::Error::NonFinite
                                                                 ? "expected finite number"
                                                                 : "expected number");
                                        });
        }

        // 冻结配置拒绝未知/重复键；否则拼错预算会静默回落到旧占位参数。
        void checkFields(const cv::FileNode &node, const std::filesystem::path &file,
                         const std::string &field, std::initializer_list<const char *> allowed)
        {
            if (!node.isMap())
                throwConfigError(file, field, "expected map");
            std::set<std::string> names, valid(allowed.begin(), allowed.end());
            for (const auto &value : node)
            {
                auto name = value.name();
                if (!valid.count(name))
                    throwConfigError(file, field + "." + name, "unknown field");
                if (!names.insert(name).second)
                    throwConfigError(file, field + "." + name, "duplicate field");
            }
        }

    } // namespace

    // 从 YAML 文件读取配置，并转换成内部强类型配置。(把 YAML 里的文字配置，变成 C++ 能使用的结构。) 第一个函数！！！
    AppConfig loadConfig(const std::filesystem::path &path)
    {
        cv::FileStorage fs(path.string(), cv::FileStorage::READ);

        if (!fs.isOpened())
        {
            throw ConfigError("Config error: file=" + path.string() + ", reason=cannot open file");
        }

        AppConfig config;

        // 在转换强类型之前逐层检查结构，保留OpenCV FileStorage单一加载路径。
        checkFields(
            fs.root(), path, "root",
            {"schema_version", "input", "preprocess", "detector", "geometry", "output", "debug"});
        checkFields(fs["input"], path, "input", {"pixel_format", "timestamp_unit"});
        checkFields(fs["preprocess"], path, "preprocess",
                    {"work_width", "work_height", "threshold"});
        checkFields(fs["geometry"], path, "geometry",
                    {"white_threshold", "approximation_epsilon", "min_area"});
        checkFields(fs["detector"], path, "detector", {"mode", "corner"});
        checkFields(fs["detector"]["corner"], path, "detector.corner",
                    {"physical_to_screen", "min_line_points", "max_line_fit_error",
                     "min_intersection_angle_deg", "max_corner_error", "approximation_epsilon",
                     "max_pair_disagreement_px2", "observation"});
        checkFields(fs["output"], path, "output",
                    {"run_directory", "video_fourcc", "video_filename"});
        checkFields(fs["debug"], path, "debug", {"timing_enabled"});

        // Q02：算法字段写入检测配置，显示与运行字段写入应用配置。
        DetectorConfig &detector = config.detector_config;

        auto requireNode = [&](const std::string &field) -> cv::FileNode
        {
            cv::FileNode node = fs[field];

            if (node.empty())
            {
                throwConfigError(path, field, "missing required field");
            }

            return node;
        };

        detector.schema_version = readInt(requireNode("schema_version"), path, "schema_version");

        cv::FileNode input = requireNode("input");

        if (input["pixel_format"].empty())
        {
            throwConfigError(path, "input.pixel_format", "missing required field");
        }

        input["pixel_format"] >> detector.input.pixel_format;

        if (input["timestamp_unit"].empty())
        {
            throwConfigError(path, "input.timestamp_unit", "missing required field");
        }

        input["timestamp_unit"] >> detector.input.timestamp_unit;

        cv::FileNode preprocess = requireNode("preprocess");

        detector.preprocess.work_width =
            readInt(preprocess["work_width"], path, "preprocess.work_width");

        detector.preprocess.work_height =
            readInt(preprocess["work_height"], path, "preprocess.work_height");

        detector.preprocess.threshold =
            readInt(preprocess["threshold"], path, "preprocess.threshold");

        // Block 2 几何观测配置。
        // 控制白色区域提取、多边形简化和面积过滤。
        cv::FileNode geometry = requireNode("geometry");

        detector.geometry_.white_threshold_ =
            readInt(geometry["white_threshold"], path, "geometry.white_threshold");

        detector.geometry_.approximation_epsilon_ =
            readDouble(geometry["approximation_epsilon"], path, "geometry.approximation_epsilon");

        detector.geometry_.min_area_ = readDouble(geometry["min_area"], path, "geometry.min_area");

        cv::FileNode detector_node = requireNode("detector");

        // Block 3 角点恢复参数。
        cv::FileNode corner_node = detector_node["corner"];

        const auto mapping = corner_node["physical_to_screen"];
        if (!mapping.isSeq() || mapping.size() != 4)
            throwConfigError(path, "corner.physical_to_screen", "required permutation of 0..3");
        for (int i = 0; i < 4; ++i)
            detector.corner_.physical_to_screen_[i] =
                readInt(mapping[i], path, "corner.physical_to_screen");

        detector.corner_.min_line_points_ =
            readInt(corner_node["min_line_points"], path, "corner.min_line_points");

        detector.corner_.max_line_fit_error_ =
            readDouble(corner_node["max_line_fit_error"], path, "corner.max_line_fit_error");

        detector.corner_.min_intersection_angle_deg_ = readDouble(
            corner_node["min_intersection_angle_deg"], path, "corner.min_intersection_angle_deg");

        detector.corner_.max_corner_error_ =
            readDouble(corner_node["max_corner_error"], path, "corner.max_corner_error");

        detector.corner_.approximation_epsilon_ =
            readDouble(corner_node["approximation_epsilon"], path, "corner.approximation_epsilon");

        detector.corner_.max_pair_disagreement_px2_ = readDouble(
            corner_node["max_pair_disagreement_px2"], path, "corner.max_pair_disagreement_px2");

        // 先检查字段存在，再检查类型，避免 YAML 类型错误被直接转换掩盖。
        // 新预算节点可缺省为显式未配置；出现后每个字段必填，不从旧参数继承数值。
        if (!corner_node["observation"].empty())
        {
            auto node = corner_node["observation"];
            checkFields(node, path, "detector.corner.observation",
                        {"max_edge_direction_diff_deg", "max_edge_position_distance_px",
                         "turn_trim_distance_px", "max_turn_connection_length_px",
                         "max_support_extension_px", "min_support_span_px"});
            CornerObservationBudget b{};
            const std::pair<const char *, double *> fields[] = {
                {"max_edge_direction_diff_deg", &b.max_edge_direction_diff_deg},
                {"max_edge_position_distance_px", &b.max_edge_position_distance_px},
                {"turn_trim_distance_px", &b.turn_trim_distance_px},
                {"max_turn_connection_length_px", &b.max_turn_connection_length_px},
                {"max_support_extension_px", &b.max_support_extension_px},
                {"min_support_span_px", &b.min_support_span_px}};
            for (auto f : fields)
                *f.second = readDouble(node[f.first], path,
                                       std::string("detector.corner.observation.") + f.first);
            detector.corner_.observation_budget_ = b;
        }

        cv::FileNode mode_node = detector_node["mode"];

        if (mode_node.empty())
        {
            throwConfigError(path, "detector.mode", "missing required field");
        }

        if (!mode_node.isString())
        {
            throwConfigError(path, "detector.mode", "expected string");
        }

        std::string mode;
        mode_node >> mode;

        if (mode == "SegmentedMark")
        {
            detector.mode = DetectorMode::SegmentedMark;
        }
        else
        {
            throwConfigError(path, "detector.mode", "unsupported mode");
        }

        const auto debug = requireNode("debug");
        detector.debug.timing_enabled =
            readBool(debug["timing_enabled"], path, "debug.timing_enabled");
        const auto output = requireNode("output");
        if (!output["run_directory"].empty())
            config.offline.directory =
                readString(output["run_directory"], path, "output.run_directory");
        if (!output["video_fourcc"].empty())
            config.offline.video_fourcc =
                readString(output["video_fourcc"], path, "output.video_fourcc");
        if (!output["video_filename"].empty())
            config.offline.video_filename =
                readString(output["video_filename"], path, "output.video_filename");

        validateConfig(config);

        return config;
    }

    // Q03：检测输入/几何/角点与模式的合法性；不读取显示、导出或诊断配置。
    void validateDetectorConfig(const DetectorConfig &detector)
    {
        if (detector.schema_version != 1)
        {
            throw ConfigError("Config error: field=schema_version, reason=unsupported version");
        }

        if (detector.input.pixel_format != "BGR8")
        {
            throw ConfigError("Config error: field=input.pixel_format, reason=unsupported format");
        }

        if (detector.input.timestamp_unit != "us")
        {
            throw ConfigError("Config error: field=input.timestamp_unit, reason=unsupported unit");
        }

        if (detector.preprocess.work_width <= 0)
        {
            throw ConfigError("Config error: field=preprocess.work_width, reason=must be positive");
        }

        if (detector.preprocess.work_height <= 0)
        {
            throw ConfigError(
                "Config error: field=preprocess.work_height, reason=must be positive");
        }

        if (detector.preprocess.threshold < 0 || detector.preprocess.threshold > 255)
        {
            throw ConfigError("Config error: field=preprocess.threshold, reason=range 0-255");
        }

        auto mapping = detector.corner_.physical_to_screen_;
        std::sort(mapping.begin(), mapping.end());
        if (mapping != std::array<int, 4>{{0, 1, 2, 3}})
            throw ConfigError(
                "Config error: field=corner.physical_to_screen, reason=required permutation of 0..3");

        // geometry.white_threshold 与灰度阈值范围一致。
        if (detector.geometry_.white_threshold_ < 0 || detector.geometry_.white_threshold_ > 255)
        {
            throw ConfigError("Config error: field=geometry.white_threshold, reason=range 0-255");
        }

        // approxPolyDP 的 epsilon 不允许为负。
        if (!std::isfinite(detector.geometry_.approximation_epsilon_) ||
            detector.geometry_.approximation_epsilon_ < 0)
        {
            throw ConfigError(
                "Config error: field=geometry.approximation_epsilon, reason=must be non-negative");
        }

        // 面积过滤下限不允许为负。
        if (!std::isfinite(detector.geometry_.min_area_) || detector.geometry_.min_area_ < 0)
        {
            throw ConfigError("Config error: field=geometry.min_area, reason=must be non-negative");
        }

        // Block 3 corner 参数校验
        const auto &corner = detector.corner_;

        if (corner.min_line_points_ < 3)
        {
            throw ConfigError("Config error: field=corner.min_line_points, reason=must be >= 3");
        }

        if (!std::isfinite(corner.max_line_fit_error_) || corner.max_line_fit_error_ < 0.0)
        {
            throw ConfigError(
                "Config error: field=corner.max_line_fit_error, reason=must be non-negative and finite");
        }

        if (!std::isfinite(corner.min_intersection_angle_deg_) ||
            corner.min_intersection_angle_deg_ <= 0.0 || corner.min_intersection_angle_deg_ > 90.0)
        {
            throw ConfigError(
                "Config error: field=corner.min_intersection_angle_deg, reason=must be finite in (0,90]");
        }

        if (!std::isfinite(corner.max_corner_error_) || corner.max_corner_error_ < 0.0)
        {
            throw ConfigError(
                "Config error: field=corner.max_corner_error, reason=must be non-negative and finite");
        }

        if (!std::isfinite(corner.approximation_epsilon_) || corner.approximation_epsilon_ <= 0.0)
        {
            throw ConfigError(
                "Config error: field=corner.approximation_epsilon, reason=must be positive");
        }

        if (!std::isfinite(corner.max_pair_disagreement_px2_) ||
            corner.max_pair_disagreement_px2_ < 0)
            throw ConfigError("Config error: corner.max_pair_disagreement_px2");
        if (corner.observation_budget_)
            validateCornerObservationBudget(corner);

        if (detector.mode != DetectorMode::SegmentedMark)
        {
            throw ConfigError("Config error: field=detector.mode, reason=unsupported mode");
        }
    }

    void validateOfflineConfig(const AppConfig &config)
    {
        if (config.offline.directory.empty() || config.offline.video_fourcc != "MJPG" ||
            config.offline.video_filename != "overlay.avi")
            throw ConfigError("Config error: output directory/video parameters");
    }

    // Q03 总入口保留原检查顺序和错误语义，组合各模块的独立校验。
    void validateConfig(const AppConfig &config)
    {
        validateDetectorConfig(config.detector_config);
        validateOfflineConfig(config);
    }

    // 将当前有效配置写出，并保证重新加载后配置值保持一致。(把程序最终实际使用的配置保存下来,用于debug，复现实验，记录比赛参数)
    /* 流程：
    用户yaml

    ↓

    loadConfig

    ↓

    补默认值/验证

    ↓

    当前实际配置

    ↓

    writeEffectiveConfig

    ↓

    保存
    */
    void writeEffectiveConfig(const AppConfig &config, const std::filesystem::path &path)
    {
        validateConfig(config); // 导出前校验，不能将非法或未往返的预算落盘。
        cv::FileStorage fs(path.string(), cv::FileStorage::WRITE);

        if (!fs.isOpened())
        {
            throw ConfigError("Config error: file=" + path.string() + ", reason=cannot write file");
        }

        const DetectorConfig &detector = config.detector_config;

        fs << "schema_version" << detector.schema_version;

        fs << "input"
           << "{";

        fs << "pixel_format" << detector.input.pixel_format;

        fs << "timestamp_unit" << detector.input.timestamp_unit;

        fs << "}";

        fs << "preprocess"
           << "{";

        fs << "work_width" << detector.preprocess.work_width;

        fs << "work_height" << detector.preprocess.work_height;

        fs << "threshold" << detector.preprocess.threshold;

        fs << "}";

        fs << "geometry"
           << "{";

        fs << "white_threshold" << detector.geometry_.white_threshold_;

        fs << "approximation_epsilon" << detector.geometry_.approximation_epsilon_;

        fs << "min_area" << detector.geometry_.min_area_;

        fs << "}";

        fs << "detector"
           << "{";

        fs << "mode"
           << "SegmentedMark";

        // 结构对齐（写和读的位置要一致）
        fs << "corner"
           << "{";

        fs << "physical_to_screen" << "[";
        for (int slot : detector.corner_.physical_to_screen_)
            fs << slot;
        fs << "]";
        fs << "min_line_points" << detector.corner_.min_line_points_;

        fs << "max_line_fit_error" << detector.corner_.max_line_fit_error_;

        fs << "min_intersection_angle_deg" << detector.corner_.min_intersection_angle_deg_;

        fs << "max_corner_error" << detector.corner_.max_corner_error_;

        fs << "approximation_epsilon" << detector.corner_.approximation_epsilon_;

        fs << "max_pair_disagreement_px2" << detector.corner_.max_pair_disagreement_px2_;
        if (detector.corner_.observation_budget_)
        {
            const auto &b = *detector.corner_.observation_budget_;
            fs << "observation" << "{";
            fs << "max_edge_direction_diff_deg" << b.max_edge_direction_diff_deg
               << "max_edge_position_distance_px" << b.max_edge_position_distance_px
               << "turn_trim_distance_px" << b.turn_trim_distance_px
               << "max_turn_connection_length_px" << b.max_turn_connection_length_px
               << "max_support_extension_px" << b.max_support_extension_px << "min_support_span_px"
               << b.min_support_span_px << "}";
        }

        fs << "}";

        fs << "}";

        fs << "output" << "{" << "run_directory" << config.offline.directory << "video_fourcc"
           << config.offline.video_fourcc << "video_filename" << config.offline.video_filename
           << "}";
        fs << "debug" << "{" << "timing_enabled" << int(detector.debug.timing_enabled) << "}";
    }
} // namespace mark
