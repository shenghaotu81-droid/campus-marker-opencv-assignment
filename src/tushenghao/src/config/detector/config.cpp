// Q01→Q03→Q06：严格读取、分模块校验、导出生效配置；预算值仅来自输入。
#include "detector/config.hpp"
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
        bool readBool(const cv::FileNode &node,const std::filesystem::path &file,const std::string &field)
        {
            return config_value::boolean(node,[&](auto){throwConfigError(file,field,"expected integer 0/1");});
        }
        int readInt(const cv::FileNode &node,const std::filesystem::path &file,const std::string &field)
        {
            return config_value::integer(node,[&](auto){throwConfigError(file,field,"expected integer");});
        }
        std::string readString(const cv::FileNode &node,const std::filesystem::path &file,const std::string &field)
        {
            return config_value::text(node,[&](auto){throwConfigError(file,field,"expected string");});
        }
        double readDouble(const cv::FileNode &node,const std::filesystem::path &file,const std::string &field)
        {
            return config_value::number(node,config_value::NumericPolicy::NodeNumber,[&](auto error){
                throwConfigError(file,field,error==config_value::Error::NonFinite?"expected finite number":"expected number");
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
        checkFields(fs.root(), path, "root",
                    {"schema_version", "input", "preprocess", "marker_geometry_path", "detector",
                     "geometry", "output", "debug"});
        checkFields(fs["input"], path, "input", {"pixel_format", "timestamp_unit"});
        checkFields(fs["preprocess"], path, "preprocess",
                    {"work_width", "work_height", "threshold"});
        checkFields(fs["geometry"], path, "geometry",
                    {"white_threshold", "approximation_epsilon", "min_area", "max_hypothesis_count",
                     "max_validation_residual", "min_area_ratio", "max_area_ratio"});
        checkFields(fs["detector"], path, "detector", {"mode", "corner", "assignment_completion"});
        checkFields(fs["detector"]["corner"], path, "detector.corner",
                    {"min_line_points", "max_line_fit_error",
                     "min_intersection_angle_deg", "max_corner_error",
                     "approximation_epsilon",
                     "semantic_geometry_threshold", "observation"});
        checkFields(fs["output"], path, "output", {"run_directory", "video_fourcc", "video_filename"});
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

        detector.geometry_.max_validation_residual_ = readDouble(
            geometry["max_validation_residual"], path, "geometry.max_validation_residual");

        detector.geometry_.min_area_ratio_ =
            readDouble(geometry["min_area_ratio"], path, "geometry.min_area_ratio");

        detector.geometry_.max_area_ratio_ =
            readDouble(geometry["max_area_ratio"], path, "geometry.max_area_ratio");

        // 几何假设搜索资源上限。
        detector.geometry_.max_hypothesis_count_ = static_cast<std::size_t>(
            readInt(geometry["max_hypothesis_count"], path, "geometry.max_hypothesis_count"));

        // MARK 几何模型文件路径（Block 2 加载用）。
        detector.marker_geometry_path_ =
            readString(fs["marker_geometry_path"], path, "marker_geometry_path");

        cv::FileNode detector_node = requireNode("detector");

        // Block 3 角点恢复参数。
        cv::FileNode corner_node = detector_node["corner"];


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


        detector.corner_.semantic_geometry_threshold_ = readDouble(
            corner_node["semantic_geometry_threshold"], path, "corner.semantic_geometry_threshold");

        // 先检查字段存在，再检查类型，避免 YAML 类型错误被直接转换掩盖。
        // 新预算节点可缺省为显式未配置；出现后每个字段必填，不从旧参数继承数值。
        if (!corner_node["observation"].empty())
        {
            auto node = corner_node["observation"];
            checkFields(node, path, "detector.corner.observation",
                        {"max_edge_direction_diff_deg", "max_edge_position_distance_px",
                         "max_component_mapping_distance_px", "turn_trim_distance_px",
                         "max_turn_connection_length_px", "max_support_extension_px",
                         "min_support_span_px"});
            CornerObservationBudget b{};
            const std::pair<const char *, double *> fields[] = {
                {"max_edge_direction_diff_deg", &b.max_edge_direction_diff_deg},
                {"max_edge_position_distance_px", &b.max_edge_position_distance_px},
                {"max_component_mapping_distance_px", &b.max_component_mapping_distance_px},
                {"turn_trim_distance_px", &b.turn_trim_distance_px},
                {"max_turn_connection_length_px", &b.max_turn_connection_length_px},
                {"max_support_extension_px", &b.max_support_extension_px},
                {"min_support_span_px", &b.min_support_span_px}};
            for (auto f : fields)
                *f.second = readDouble(node[f.first], path,
                                       std::string("detector.corner.observation.") + f.first);
            detector.corner_.observation_budget_ = b;
        }
        if (!detector_node["assignment_completion"].empty())
        {
            auto node = detector_node["assignment_completion"];
            checkFields(node, path, "detector.assignment_completion",
                        {"max_boundary_distance_work_px", "approximation_epsilon_work_px",
                         "max_direction_diff_deg", "max_relative_area_error",
                         "boundary_sample_step_work_px", "max_candidates", "max_expansions",
                         "max_output_branches"});
            AssignmentCompletionConfig b{};
            const std::pair<const char *, double *> fields[] = {
                {"max_boundary_distance_work_px", &b.max_boundary_distance_work_px},
                {"approximation_epsilon_work_px", &b.approximation_epsilon_work_px},
                {"max_direction_diff_deg", &b.max_direction_diff_deg},
                {"max_relative_area_error", &b.max_relative_area_error},
                {"boundary_sample_step_work_px", &b.boundary_sample_step_work_px}};
            for (auto f : fields)
                *f.second = readDouble(node[f.first], path,
                                       std::string("detector.assignment_completion.") + f.first);
            const std::pair<const char *, size_t *> counts[] = {
                {"max_candidates", &b.max_candidates},
                {"max_expansions", &b.max_expansions},
                {"max_output_branches", &b.max_output_branches}};
            for (auto f : counts)
            {
                int value = readInt(node[f.first], path,
                                    std::string("detector.assignment_completion.") + f.first);
                if (value <= 0)
                    throwConfigError(path, f.first, "expected positive count");
                *f.second = static_cast<size_t>(value);
            }
            detector.assignment_completion_ = b;
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
        detector.debug.timing_enabled = readBool(debug["timing_enabled"], path, "debug.timing_enabled");
        const auto output = requireNode("output");
        if (!output["run_directory"].empty())
            config.offline.directory = readString(output["run_directory"], path, "output.run_directory");
        if (!output["video_fourcc"].empty())
            config.offline.video_fourcc = readString(output["video_fourcc"], path, "output.video_fourcc");
        if (!output["video_filename"].empty())
            config.offline.video_filename = readString(output["video_filename"], path, "output.video_filename");

        // marker_geometry_path_ 若为相对路径，转成相对于 detector.yaml 所在目录的绝对路径。
        // 消除 CWD 依赖，Block 4/5 及 Codex 不用再 cd。
        {
            namespace fs = std::filesystem;
            fs::path yaml_dir = fs::path(path).parent_path();
            fs::path geom_path(config.detector_config.marker_geometry_path_);
            if (geom_path.is_relative())
            {
                geom_path = yaml_dir / geom_path;
                config.detector_config.marker_geometry_path_ = geom_path.string();
            }
        }

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

        // 搜索上限必须大于 0。
        if (detector.geometry_.max_hypothesis_count_ == 0)
        {
            throw ConfigError(
                "Config error: field=geometry.max_hypothesis_count, reason=must be greater than zero");
        }

        // Step 7 残差验证阈值必须为正
        if (!std::isfinite(detector.geometry_.max_validation_residual_) ||
            detector.geometry_.max_validation_residual_ <= 0)
        {
            throw ConfigError(
                "Config error: field=geometry.max_validation_residual, reason=must be positive");
        }

        // Step 7 面积比例范围检查
        if (!std::isfinite(detector.geometry_.min_area_ratio_) ||
            !std::isfinite(detector.geometry_.max_area_ratio_) ||
            detector.geometry_.min_area_ratio_ <= 0 || detector.geometry_.max_area_ratio_ <= 0 ||
            detector.geometry_.min_area_ratio_ >= detector.geometry_.max_area_ratio_)
        {
            throw ConfigError("Config error: field=geometry.area_ratio, reason=invalid range");
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

        if (!std::isfinite(detector.corner_.semantic_geometry_threshold_) ||
            detector.corner_.semantic_geometry_threshold_ < 0.0)
        {
            throw ConfigError(
                "Config error: field=corner.semantic_geometry_threshold, reason=must be non-negative and finite");
        }

        if (!std::isfinite(corner.approximation_epsilon_) || corner.approximation_epsilon_ <= 0.0)
        {
            throw ConfigError(
                "Config error: field=corner.approximation_epsilon, reason=must be positive");
        }


        if (corner.observation_budget_)
            validateCornerObservationBudget(corner);
        if (detector.assignment_completion_)
        {
            const auto &b = *detector.assignment_completion_;
            for (double value : {b.max_boundary_distance_work_px, b.approximation_epsilon_work_px,
                                 b.max_direction_diff_deg, b.max_relative_area_error,
                                 b.boundary_sample_step_work_px})
                if (!std::isfinite(value) || value < 0)
                    throw ConfigError(
                        "ASSIGNMENT_CONFIG_INVALID: nonnegative finite budget required");
            if (b.approximation_epsilon_work_px <= 0 || b.boundary_sample_step_work_px <= 0 ||
                b.max_direction_diff_deg > 90 || !b.max_candidates || !b.max_expansions ||
                !b.max_output_branches ||
                b.max_candidates > static_cast<size_t>(std::numeric_limits<int>::max()) ||
                b.max_expansions > static_cast<size_t>(std::numeric_limits<int>::max()) ||
                b.max_output_branches > static_cast<size_t>(std::numeric_limits<int>::max()))
                throw ConfigError("ASSIGNMENT_CONFIG_INVALID: invalid sample/resource budget");
        }

        // MARK 几何模型路径不能为空。
        if (detector.marker_geometry_path_.empty())
        {
            throw ConfigError(
                "Config error: field=marker_geometry_path, reason=path must not be empty");
        }

        if (detector.mode != DetectorMode::SegmentedMark)
        {
            throw ConfigError("Config error: field=detector.mode, reason=unsupported mode");
        }

    }

    void validateOfflineConfig(const AppConfig &config)
    {
        if (config.offline.directory.empty() || config.offline.video_fourcc != "MJPG" ||
            config.offline.video_filename != "overlay.mp4")
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

        fs << "max_hypothesis_count" << static_cast<int>(detector.geometry_.max_hypothesis_count_);

        fs << "max_validation_residual" << detector.geometry_.max_validation_residual_;

        fs << "min_area_ratio" << detector.geometry_.min_area_ratio_;

        fs << "max_area_ratio" << detector.geometry_.max_area_ratio_;

        fs << "}";

        fs << "marker_geometry_path" << detector.marker_geometry_path_;

        fs << "detector"
           << "{";

        fs << "mode"
           << "SegmentedMark";

        // 结构对齐（写和读的位置要一致）
        fs << "corner"
           << "{";


        fs << "min_line_points" << detector.corner_.min_line_points_;

        fs << "max_line_fit_error" << detector.corner_.max_line_fit_error_;

        fs << "min_intersection_angle_deg" << detector.corner_.min_intersection_angle_deg_;

        fs << "max_corner_error" << detector.corner_.max_corner_error_;


        fs << "approximation_epsilon" << detector.corner_.approximation_epsilon_;


        fs << "semantic_geometry_threshold" << detector.corner_.semantic_geometry_threshold_;

        if (detector.corner_.observation_budget_)
        {
            const auto &b = *detector.corner_.observation_budget_;
            fs << "observation" << "{";
            fs << "max_edge_direction_diff_deg" << b.max_edge_direction_diff_deg
               << "max_edge_position_distance_px" << b.max_edge_position_distance_px
               << "max_component_mapping_distance_px" << b.max_component_mapping_distance_px
               << "turn_trim_distance_px" << b.turn_trim_distance_px
               << "max_turn_connection_length_px" << b.max_turn_connection_length_px
               << "max_support_extension_px" << b.max_support_extension_px << "min_support_span_px"
               << b.min_support_span_px << "}";
        }

        fs << "}";

        if (detector.assignment_completion_)
        {
            const auto &b = *detector.assignment_completion_;
            fs << "assignment_completion" << "{";
            fs << "max_boundary_distance_work_px" << b.max_boundary_distance_work_px
               << "approximation_epsilon_work_px" << b.approximation_epsilon_work_px
               << "max_direction_diff_deg" << b.max_direction_diff_deg << "max_relative_area_error"
               << b.max_relative_area_error << "boundary_sample_step_work_px"
               << b.boundary_sample_step_work_px << "max_candidates"
               << static_cast<int>(b.max_candidates) << "max_expansions"
               << static_cast<int>(b.max_expansions) << "max_output_branches"
               << static_cast<int>(b.max_output_branches) << "}";
        }

        fs << "}";

        fs << "output" << "{" << "run_directory" << config.offline.directory
           << "video_fourcc" << config.offline.video_fourcc
           << "video_filename" << config.offline.video_filename << "}";
        fs << "debug" << "{" << "timing_enabled" << int(detector.debug.timing_enabled) << "}";
    }
} // namespace mark
