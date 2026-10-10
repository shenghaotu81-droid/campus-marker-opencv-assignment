// 黑盒！！！
// 读->验->写（三个函数）(读取 + 检查 + 转换) 具体实现暂时黑盒（时间不足以完全学会，以后遇到要改动再展开第二层（requireNode、readBool/readInt、lambda、错误设计）实现细节）
// 工程定位：负责把外部 YAML 配置文件，转换成程序内部可靠的 C++ 配置对象，并保证进入 Detector 前配置合法。
/* 报错表格配置板块契约（看懂就能理解报错输出、能验收、能调试）：
1.loadConfig 抛错（读的时候）：
->文件不存在/打不开
->必填字段缺失
->类型错误（开关不是 0/1 整数、mode 不是字符串）
->mode 不是 "skeleton"

2.validateConfig 抛错（验的时候）：
->schema_version ≠ 1、pixel_format ≠ BGR8、timestamp_unit ≠ "us"
->work_width/height ≤ 0、threshold 超出 0~255、max_hold_frames < 0
->Block5绘制/计时开关被打开

3.writeEffectiveConfig 抛错：
->文件写不开

错误信息格式统一：file=哪个文件, field=哪个字段, reason=为啥错。
*/
/* 加字段后续可能增量：
1. struct 加成员；
2. loadConfig 加一段"读"；
3. validateConfig 加一条"验"；
4. writeEffectiveConfig 加一段"写"；
5. detector.yaml 加一行；
6. schema_version 升版（冻结要求"新增参数须版本化"）扩展点明确，不用碰老代码。
*/
/*  大体思路：
1. loadConfig(path)——读
打开 YAML 文件 → 逐个读 14 个字段。每个字段三步：看在不在（缺了抛错）、看类型对不对（开关必须是整数 0/1）、转成 C++ 类型（0/1→bool，"skeleton"→枚举）。读完再调 validateConfig 验一遍，返回填好的配置。
2. validateConfig(config)——验
不碰文件，只检查内存里的 struct。一条条过：版本号是不是 1、宽高是不是正数、阈值在不在 0~255、那几个"本板块不可启用"的开关有没有被打开（开了就报错"未实现"）。一条不过就抛 ConfigError。
3. writeEffectiveConfig(config, path)——写
把配置写回 YAML 文件。用途是导出"实际生效的配置"留档。要求：写出去再读回来，值必须一样（往返一致）。

两个小帮手：
throwConfigError：统一抛错，错误信息里带"哪个文件、哪个字段、为啥错"
readBool：读 0/1 开关，顺手校验只能是 0 或 1
*/
#include "config/config.hpp"
#include "core/corner_budget.hpp"
#include <set>
#include <fstream>
#include <regex>
#include <limits>

#include <opencv2/core.hpp>

#include "core/config_error.hpp"

#include <filesystem>

// #include<iostream>  // debug临时加的

namespace mark
{

    namespace
    { // 配置错误：文件=<path>，字段=<field>，原因=<reason>

        // 错误信息中英文对照：
        //   missing required field  → 缺少必填字段
        //   expected integer        → 应为整数
        //   expected integer 0/1    → 应为整数 0 或 1
        //   expected string         → 应为字符串
        //   unsupported mode        → 不支持的模式（只认 skeleton）
        //   unsupported version     → 不支持的版本（只认 1）
        //   unsupported format      → 不支持的格式（只认 BGR8）
        //   unsupported unit        → 不支持的单位（只认 us）
        //   must be positive        → 必须为正数（>0）
        //   must be non-negative    → 必须为非负数（≥0）
        //   range 0-255             → 超出范围 0~255
        //   cannot open file        → 无法打开文件
        //   cannot write file       → 无法写入文件
        //   not implemented         → 该功能在本板块未实现

        // 统一生成配置错误，保证错误信息包含文件路径和字段路径。
        [[noreturn]] void throwConfigError(const std::filesystem::path &file,
                                           const std::string &field, const std::string &message)
        {
            throw ConfigError("Config error: file=" + file.string() + ", field=" + field +
                              ", reason=" + message);
        }

        // 读取 YAML 中的开关字段，并转换成 C++ bool。
        bool readBool(const cv::FileNode &node, const std::filesystem::path &file,
                      const std::string &field)
        {
            if (!node.isInt())
            {
                throwConfigError(file, field, "expected integer 0/1");
            }

            int value = 0;
            node >> value;

            if (value != 0 && value != 1)
            {
                throwConfigError(file, field, "expected integer 0/1");
            }

            return value == 1;
        }

        // 读取整数字段并检查基础类型。
        int readInt(const cv::FileNode &node, const std::filesystem::path &file,
                    const std::string &field)
        {
            if (!node.isInt())
            {
                throwConfigError(file, field, "expected integer");
            }

            int value = 0;
            node >> value;

            return value;
        }

        // 读取字符串字段并检查基础类型。
        std::string readString(const cv::FileNode &node, const std::filesystem::path &file,
                               const std::string &field)
        {
            if (!node.isString())
            {
                throwConfigError(file, field, "expected string");
            }

            std::string value;
            node >> value;

            return value;
        }

        // 读取浮点字段，并检查 YAML 基础类型。(避免 OpenCV YAML 类型坑)
        double readDouble(const cv::FileNode &node, const std::filesystem::path &file,
                          const std::string &field)
        {
            if (!node.isReal() && !node.isInt())
            {
                throwConfigError(file, field, "expected number");
            }

            double value = 0.0;

            node >> value;
            // NaN/Inf 在范围比较中可能全部为假，不能被当作合法预算。
            if (!std::isfinite(value))
                throwConfigError(file, field, "expected finite number");
            return value;
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
                     "geometry", "temporal", "output", "debug"});
        checkFields(fs["input"], path, "input", {"pixel_format", "timestamp_unit"});
        checkFields(fs["preprocess"], path, "preprocess",
                    {"work_width", "work_height", "threshold"});
        checkFields(fs["geometry"], path, "geometry",
                    {"white_threshold", "approximation_epsilon", "min_area", "max_hypothesis_count",
                     "max_validation_residual", "min_area_ratio", "max_area_ratio"});
        checkFields(fs["detector"], path, "detector", {"mode", "corner", "assignment_completion"});
        checkFields(fs["detector"]["corner"], path, "detector.corner",
                    {"local_search_margin_ratio", "min_line_points", "max_line_fit_error",
                     "min_intersection_angle_deg", "max_corner_error", "reject_truncated_corner",
                     "approximation_epsilon", "edge_point_distance_threshold",
                     "semantic_geometry_threshold", "observation"});
        checkFields(fs["temporal"], path, "temporal",
                    {"stabilization_enabled", "display_hold_enabled", "max_hold_frames",
                     "reference_dt_ms", "reference_alpha", "history_max_gap_ms",
                     "max_center_distance_diagonal_ratio", "min_area_ratio", "max_area_ratio",
                     "correspondence_uncertainty_px", "max_smoothing_deviation_px"});
        checkFields(fs["output"], path, "output",
                    {"show_window", "show_held_state", "run_mode", "run_directory",
                     "export_evidence", "export_video", "playback_fps", "expected_frame_count",
                     "video_fourcc", "video_filename", "display_probe_timeout_ms"});
        checkFields(fs["debug"], path, "debug",
                    {"timing_enabled", "draw_candidates", "level", "detail_first", "detail_last",
                     "detail_interval", "draw_raw", "draw_stable", "draw_corner_evidence",
                     "draw_timing"});

        // YAML 字段属于 DetectorConfig，因此统一从这里访问，避免应用层和检测器配置混淆。
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

        detector.corner_.local_search_margin_ratio_ = readDouble(
            corner_node["local_search_margin_ratio"], path, "corner.local_search_margin_ratio");

        detector.corner_.min_line_points_ =
            readInt(corner_node["min_line_points"], path, "corner.min_line_points");

        detector.corner_.max_line_fit_error_ =
            readDouble(corner_node["max_line_fit_error"], path, "corner.max_line_fit_error");

        detector.corner_.min_intersection_angle_deg_ = readDouble(
            corner_node["min_intersection_angle_deg"], path, "corner.min_intersection_angle_deg");

        detector.corner_.max_corner_error_ =
            readDouble(corner_node["max_corner_error"], path, "corner.max_corner_error");

        detector.corner_.reject_truncated_corner_ = readBool(
            corner_node["reject_truncated_corner"], path, "corner.reject_truncated_corner");

        detector.corner_.approximation_epsilon_ =
            readDouble(corner_node["approximation_epsilon"], path, "corner.approximation_epsilon");

        detector.corner_.edge_point_distance_threshold_ =
            readDouble(corner_node["edge_point_distance_threshold"], path,
                       "corner.edge_point_distance_threshold");

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

        /* debug 工程经验：OpenCV cv::FileStorage 的 YAML 注释兼容性与标准 YAML 不完全一致，配置文件中避免使用行尾 # comment，否则可能被读取为字符串内容。
        这三行是"用数字代替眼睛"：

        1. mode.size() —— 字符串长度。"Skeleton" 该是 8，读出来 59，直接暴露多了东西

        2. for 循环打印每个字符的 ASCII 码 —— 比如 S=83、k=107、空格=32。肉眼看不见的字符（空格、换行、中文），在数字面前全现形

        3. mode=[...] —— 前后加方括号，原样打印。一眼看出开头结尾有没有偷藏空格

        4. 原理："看着对但跑不对"时，别用眼睛猜，用数字说话。 size=59 就是铁证——字符串里混进了注释。
        // debug 临时加的
        std::cout << "mode size = " << mode.size() << std::endl;

        std::cout << "mode chars:";
        for (unsigned char c : mode)
        {
            std::cout << " [" << static_cast<int>(c) << "]";
        }
        std::cout << std::endl;

        std::cout << "mode=[" << mode << "]" << std::endl;
        // debug end
        */

        if (mode == "Skeleton")
        {
            detector.mode = DetectorMode::Skeleton;
        }
        else
        {
            throwConfigError(path, "detector.mode", "unsupported mode");
        }

        cv::FileNode temporal = requireNode("temporal");

        detector.temporal.stabilization_enabled =
            readBool(temporal["stabilization_enabled"], path, "temporal.stabilization_enabled");

        detector.temporal.display_hold_enabled =
            readBool(temporal["display_hold_enabled"], path, "temporal.display_hold_enabled");

        detector.temporal.max_hold_frames =
            readInt(temporal["max_hold_frames"], path, "temporal.max_hold_frames");

        // schema=1旧快照允许省略冻结起点；G-B缺失保持optional空，不能自动批准。
        if (!temporal["reference_dt_ms"].empty())
            detector.temporal.reference_dt_ms =
                readDouble(temporal["reference_dt_ms"], path, "temporal.reference_dt_ms");
        if (!temporal["reference_alpha"].empty())
            detector.temporal.reference_alpha =
                readDouble(temporal["reference_alpha"], path, "temporal.reference_alpha");
        if (!temporal["history_max_gap_ms"].empty())
            detector.temporal.history_max_gap_ms =
                readDouble(temporal["history_max_gap_ms"], path, "temporal.history_max_gap_ms");
        if (!temporal["max_center_distance_diagonal_ratio"].empty())
            detector.temporal.max_center_distance_diagonal_ratio =
                readDouble(temporal["max_center_distance_diagonal_ratio"], path,
                           "temporal.max_center_distance_diagonal_ratio");
        if (!temporal["min_area_ratio"].empty())
            detector.temporal.min_area_ratio =
                readDouble(temporal["min_area_ratio"], path, "temporal.min_area_ratio");
        if (!temporal["max_area_ratio"].empty())
            detector.temporal.max_area_ratio =
                readDouble(temporal["max_area_ratio"], path, "temporal.max_area_ratio");
        if (!temporal["correspondence_uncertainty_px"].empty())
            detector.temporal.correspondence_uncertainty_px =
                readDouble(temporal["correspondence_uncertainty_px"], path,
                           "temporal.correspondence_uncertainty_px");
        if (!temporal["max_smoothing_deviation_px"].empty())
            detector.temporal.max_smoothing_deviation_px =
                readDouble(temporal["max_smoothing_deviation_px"], path,
                           "temporal.max_smoothing_deviation_px");
        // OpenCV整数解析会先截断到32位（4294967296变0），节点值已丢失原值。
        // 仅新增hold字段在原始YAML十进制token上补溢出检查，保持旧参数规则不变。
        std::ifstream raw_file(path);
        std::string raw_text((std::istreambuf_iterator<char>(raw_file)), {});
        const std::regex hold_token(
            R"((^|[\n{,])[ \t]*(?:"max_hold_frames"|max_hold_frames)[ \t]*:[ \t]*([+-]?[0-9]+))");
        for (std::sregex_iterator it(raw_text.begin(), raw_text.end(), hold_token), end; it != end;
             ++it)
        {
            try
            {
                long long value = std::stoll((*it)[2].str());
                if (value < 0 || value > std::numeric_limits<int>::max())
                    throwConfigError(path, "temporal.max_hold_frames",
                                     "integer overflow or negative");
            }
            catch (const std::out_of_range &)
            {
                throwConfigError(path, "temporal.max_hold_frames", "integer overflow");
            }
        }

        cv::FileNode output = requireNode("output");

        detector.output.show_window = readBool(output["show_window"], path, "output.show_window");

        detector.output.show_held_state =
            readBool(output["show_held_state"], path, "output.show_held_state");

        cv::FileNode debug = requireNode("debug");

        detector.debug.timing_enabled =
            readBool(debug["timing_enabled"], path, "debug.timing_enabled");

        detector.debug.draw_candidates =
            readBool(debug["draw_candidates"], path, "debug.draw_candidates");

        // 旧配置没有观测字段时采用冻结默认；存在字段仍走严格基础类型检查，不改变算法预算。
        auto optionalString = [&](const cv::FileNode &node, const char *key, std::string &value,
                                  const std::string &prefix)
        {
            if (!node[key].empty())
                value = readString(node[key], path, prefix + key);
        };
        auto optionalBool =
            [&](const cv::FileNode &node, const char *key, bool &value, const std::string &prefix)
        {
            if (!node[key].empty())
                value = readBool(node[key], path, prefix + key);
        };
        optionalString(debug, "level", config.diagnostics.level, "debug.");
        auto range = [&](const char *key) -> std::optional<uint64_t>
        {
            if (debug[key].empty())
                return std::nullopt;
            // FileStorage会先截断32位整数，先核原始十进制token，避免巨大帧号被默默改为0。
            const std::regex token(std::string("(^|[\\n{,])[ \\t]*(?:\"") + key + "\"|" + key +
                                   ")[ \\t]*:[ \\t]*([+-]?[0-9]+)");
            for (std::sregex_iterator it(raw_text.begin(), raw_text.end(), token), end; it != end;
                 ++it)
                try
                {
                    auto v = std::stoll((*it)[2].str());
                    if (v < 0 || v > std::numeric_limits<int>::max())
                        throwConfigError(path, std::string("debug.") + key,
                                         "integer overflow or negative");
                }
                catch (const std::out_of_range &)
                {
                    throwConfigError(path, std::string("debug.") + key, "integer overflow");
                }
            const auto value = readDouble(debug[key], path, std::string("debug.") + key);
            if (!debug[key].isInt() || value < 0 || value > std::numeric_limits<int>::max())
                throwConfigError(path, std::string("debug.") + key,
                                 "expected nonnegative integer within supported range");
            return static_cast<uint64_t>(value);
        };
        if (auto v = range("detail_first"))
            config.diagnostics.detail_first = *v;
        config.diagnostics.detail_last = range("detail_last");
        if (auto v = range("detail_interval"))
            config.diagnostics.detail_interval = *v;
        optionalBool(debug, "draw_raw", config.render.draw_raw, "debug.");
        optionalBool(debug, "draw_stable", config.render.draw_stable, "debug.");
        optionalBool(debug, "draw_corner_evidence", config.render.draw_corner_evidence, "debug.");
        optionalBool(debug, "draw_timing", config.render.draw_timing, "debug.");
        optionalString(output, "run_mode", config.offline.mode, "output.");
        optionalString(output, "run_directory", config.offline.directory, "output.");
        optionalBool(output, "export_evidence", config.offline.export_evidence, "output.");
        optionalBool(output, "export_video", config.offline.export_video, "output.");
        // 容器截断也可能正常 EOF；App 期待帧数单独加载，不进入 Detector 判据。
        if (!output["expected_frame_count"].empty())
        {
            if (!output["expected_frame_count"].isInt())
                throwConfigError(path, "output.expected_frame_count",
                                 "expected nonnegative integer");
            // FileStorage 的 int 只有 32 位；原始十进制 token 保留 uint64 完整范围并严格拒绝溢出。
            const std::regex token(
                R"((^|[\n{,])[ \t]*(?:"expected_frame_count"|expected_frame_count)[ \t]*:[ \t]*([^ \t\r\n,}#]+))");
            std::smatch match;
            if (!std::regex_search(raw_text, match, token))
                throwConfigError(path, "output.expected_frame_count", "missing integer token");
            const auto value = match[2].str();
            if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
                throwConfigError(path, "output.expected_frame_count",
                                 "expected nonnegative integer");
            try
            {
                config.offline.expected_frame_count = std::stoull(value);
            }
            catch (const std::exception &)
            {
                throwConfigError(path, "output.expected_frame_count", "uint64 overflow");
            }
        }
        if (!output["playback_fps"].empty())
            config.offline.playback_fps =
                readDouble(output["playback_fps"], path, "output.playback_fps");

        // 原未实现开关缺工程参数；编码和 GUI 超时只作用于 App。
        optionalString(output, "video_fourcc", config.offline.video_fourcc, "output.");
        optionalString(output, "video_filename", config.offline.video_filename, "output.");
        if (!output["display_probe_timeout_ms"].empty())
            config.offline.display_probe_timeout_ms = readInt(
                output["display_probe_timeout_ms"], path, "output.display_probe_timeout_ms");
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

    // 校验配置是否满足冻结字段要求和当前板块实现范围。（检查“配置虽然读进来了，但是能不能用”，即“格式正确，但参数错误”（合理性）；区别于loadConfig检查配置有没有。）
    void validateConfig(const AppConfig &config)
    {
        const DetectorConfig &detector = config.detector_config;

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

        if (!std::isfinite(corner.local_search_margin_ratio_) ||
            corner.local_search_margin_ratio_ <= 0.0)
        {
            throw ConfigError(
                "Config error: field=corner.local_search_margin_ratio, reason=must be positive");
        }

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

        if (!std::isfinite(corner.edge_point_distance_threshold_) ||
            corner.edge_point_distance_threshold_ <= 0.0)
        {
            throw ConfigError(
                "Config error: field=corner.edge_point_distance_threshold, reason=must be positive");
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

        // 新时序参数同时保护直接构造和YAML加载；缺预算允许NOT_READY审计。
        const auto &t = detector.temporal;
        auto positive = [](double v)
        {
            return std::isfinite(v) && v > 0;
        };
        if (!positive(t.reference_dt_ms) || !std::isfinite(t.reference_alpha) ||
            t.reference_alpha <= 0 || t.reference_alpha >= 1 || !positive(t.history_max_gap_ms) ||
            !positive(t.max_center_distance_diagonal_ratio) || !positive(t.min_area_ratio) ||
            !positive(t.max_area_ratio) || t.min_area_ratio > 1 || t.max_area_ratio < 1)
            throw ConfigError(
                "Config error: field=temporal, reason=invalid finite temporal parameter");
        if (t.correspondence_uncertainty_px && (!std::isfinite(*t.correspondence_uncertainty_px) ||
                                                *t.correspondence_uncertainty_px < 0))
            throw ConfigError(
                "Config error: field=temporal.correspondence_uncertainty_px, reason=must be finite non-negative");
        if (t.max_smoothing_deviation_px && !positive(*t.max_smoothing_deviation_px))
            throw ConfigError(
                "Config error: field=temporal.max_smoothing_deviation_px, reason=must be finite positive");

        // temporal配置
        if (detector.temporal.max_hold_frames < 0)
        {
            throw ConfigError(
                "Config error: field=temporal.max_hold_frames, reason=must be non-negative");
        }

        if (detector.mode != DetectorMode::Skeleton)
        {
            throw ConfigError("Config error: field=detector.mode, reason=unsupported mode");
        }

        // 已实现观测开关解除旧禁用；模式冲突留给runner检查，Detector构造只校验值域。
        if (config.diagnostics.level != "summary" && config.diagnostics.level != "frame" &&
            config.diagnostics.level != "evidence")
            throw ConfigError("Config error: debug.level");
        if (!config.diagnostics.detail_interval ||
            (config.diagnostics.detail_last &&
             *config.diagnostics.detail_last < config.diagnostics.detail_first))
            throw ConfigError("Config error: debug detail range/interval");
        if (config.diagnostics.detail_first > std::numeric_limits<int>::max() ||
            config.diagnostics.detail_interval > std::numeric_limits<int>::max() ||
            (config.diagnostics.detail_last &&
             *config.diagnostics.detail_last > std::numeric_limits<int>::max()))
            throw ConfigError("Config error: debug range integer overflow");
        if (config.offline.mode != "baseline" && config.offline.mode != "debug")
            throw ConfigError("Config error: output.run_mode");
        if (!std::isfinite(config.offline.playback_fps) || config.offline.playback_fps < 0)
            throw ConfigError("Config error: output.playback_fps");
        // 原视频导出全局拒绝；现在检查实际模式、固定编码/文件名及正超时。
        if (config.offline.export_video && config.offline.mode != "debug")
            throw ConfigError("Config error: baseline export_video conflict");
        if (config.offline.video_fourcc != "MJPG" ||
            config.offline.video_filename != "overlay.mp4" ||
            config.offline.display_probe_timeout_ms <= 0)
            throw ConfigError("Config error: output video/probe parameters");
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
           << "Skeleton";

        // 结构对齐（写和读的位置要一致）
        fs << "corner"
           << "{";

        fs << "local_search_margin_ratio" << detector.corner_.local_search_margin_ratio_;

        fs << "min_line_points" << detector.corner_.min_line_points_;

        fs << "max_line_fit_error" << detector.corner_.max_line_fit_error_;

        fs << "min_intersection_angle_deg" << detector.corner_.min_intersection_angle_deg_;

        fs << "max_corner_error" << detector.corner_.max_corner_error_;

        fs << "reject_truncated_corner"
           << static_cast<int>(detector.corner_.reject_truncated_corner_);

        fs << "approximation_epsilon" << detector.corner_.approximation_epsilon_;

        fs << "edge_point_distance_threshold" << detector.corner_.edge_point_distance_threshold_;

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

        fs << "temporal"
           << "{";

        fs << "stabilization_enabled" << static_cast<int>(detector.temporal.stabilization_enabled);

        fs << "reference_dt_ms" << detector.temporal.reference_dt_ms;
        fs << "reference_alpha" << detector.temporal.reference_alpha;
        fs << "history_max_gap_ms" << detector.temporal.history_max_gap_ms;
        fs << "max_center_distance_diagonal_ratio"
           << detector.temporal.max_center_distance_diagonal_ratio;
        fs << "min_area_ratio" << detector.temporal.min_area_ratio;
        fs << "max_area_ratio" << detector.temporal.max_area_ratio;
        if (detector.temporal.correspondence_uncertainty_px)
            fs << "correspondence_uncertainty_px"
               << *detector.temporal.correspondence_uncertainty_px;
        if (detector.temporal.max_smoothing_deviation_px)
            fs << "max_smoothing_deviation_px" << *detector.temporal.max_smoothing_deviation_px;

        fs << "display_hold_enabled" << static_cast<int>(detector.temporal.display_hold_enabled);

        fs << "max_hold_frames" << detector.temporal.max_hold_frames;

        fs << "}";

        fs << "output"
           << "{";

        fs << "run_mode" << config.offline.mode << "run_directory" << config.offline.directory
           << "export_evidence" << int(config.offline.export_evidence) << "export_video"
           << int(config.offline.export_video) << "playback_fps" << config.offline.playback_fps
           << "expected_frame_count" << "__FINAL_FIXES_EXPECTED_COUNT__"
           << "video_fourcc" << config.offline.video_fourcc << "video_filename"
           << config.offline.video_filename << "display_probe_timeout_ms"
           << config.offline.display_probe_timeout_ms;
        fs << "show_window" << static_cast<int>(detector.output.show_window);

        fs << "show_held_state" << static_cast<int>(detector.output.show_held_state);

        fs << "}";

        fs << "debug"
           << "{";

        fs << "level" << config.diagnostics.level << "detail_first"
           << int(config.diagnostics.detail_first) << "detail_interval"
           << int(config.diagnostics.detail_interval);
        if (config.diagnostics.detail_last)
            fs << "detail_last" << int(*config.diagnostics.detail_last);
        fs << "draw_raw" << int(config.render.draw_raw) << "draw_stable"
           << int(config.render.draw_stable) << "draw_corner_evidence"
           << int(config.render.draw_corner_evidence) << "draw_timing"
           << int(config.render.draw_timing);
        fs << "timing_enabled" << static_cast<int>(detector.debug.timing_enabled);

        fs << "draw_candidates" << static_cast<int>(detector.debug.draw_candidates);

        fs << "}";

        fs.release();
        // FileStorage 无 uint64 写入重载；只替换本次生成的唯一占位字段为精确整数，避免截断或浮点舍入。
        std::ifstream input(path);
        std::string text((std::istreambuf_iterator<char>(input)), {});
        input.close();
        const std::regex placeholder(
            R"count((expected_frame_count:[ \t]*)"?__FINAL_FIXES_EXPECTED_COUNT__"?)count");
        std::smatch match;
        if (!std::regex_search(text, match, placeholder))
            throwConfigError(path, "output.expected_frame_count", "effective placeholder missing");
        text.replace(size_t(match.position()), size_t(match.length()),
                     match[1].str() + std::to_string(config.offline.expected_frame_count));
        std::ofstream out(path);
        out << text;
        out.flush();
        if (!out)
            throwConfigError(path, "output.expected_frame_count", "effective config write failed");
    }

} // namespace mark

// 老版本留档(骨架)
/*
// config.hpp 的具体实现(骨架,还没有真正解析 YAML 字段)
#include "config.hpp"

#include <opencv2/core.hpp>

#include "config_error.hpp"

namespace mark
{
    // 根据路径读取配置文件，返回 AppConfig
    AppConfig loadConfig(const std::filesystem::path &path)
    {
        // 检查文件是否存在
        if (!std::filesystem::exists(path))
        {
            throw ConfigError(
                "Config file does not exist: " + path.string());
        }

        // 使用 OpenCV 的 FileStorage 读取 YAML 配置文件（打开 YAML）cv::FileStorage::READ表示打开模式：读取
        cv::FileStorage fs(
            path.string(),
            cv::FileStorage::READ);

        // 判断是否打开成功
        if (!fs.isOpened())
        {
            throw ConfigError(
                "Cannot open config file: " + path.string());
        }

        // 创建配置对象（空配置现在）
        AppConfig config;

        // TODO:
        // 当前只建立配置加载骨架。
        // Current implementation only provides config loading skeleton.
        //
        // TODO:
        // 等 §5.5 YAML字段契约确定后，
        // 在此解析 schema_version、input、preprocess 等字段。
        //
        // Parse schema_version, input, preprocess and other fields
        // after §5.5 YAML field contract is implemented.

        // 释放文件（关闭 YAML 文件）
        fs.release();

        // 验证配置是否合法（目前没有字段可验证）
        validateConfig(config);

        // 返回配置对象
        return config;
    }

    // 验证配置是否合法（目前没有字段可验证）
    void validateConfig(const AppConfig &config)
    {
        (void)config;

        // TODO:
        // 当前 DetectorConfig 仍为空结构，无实际字段可校验。
        // DetectorConfig has no frozen fields yet, so no validation is implemented.
        //
        // 等 §5.5 字段冻结后补充合法性检查。
        // Add validation after §5.5 fields are frozen.
    }

    // 把最终使用的配置写出去（保存“程序实际使用的最终配置”，方便复现、调试和排查问题）
    void writeEffectiveConfig(
        const AppConfig &config,
        const std::filesystem::path &path)    // std::filesystem::path ：只写文件名（在当前程序运行目录下创建）；写到指定目录；绝对路径
    {
        (void)config;

        cv::FileStorage fs(
            path.string(),
            cv::FileStorage::WRITE);         // 写文件

        if (!fs.isOpened())
        {
            throw ConfigError(
                "Cannot write config file: " + path.string());
        }

        // TODO:
        // 当前没有冻结字段可导出。
        // No frozen config fields are available for export yet.
        //
        // 等 §5.5 字段实现后写入有效配置。
        // Write effective config after §5.5 implementation.

        fs.release();
    }

} // namespace mark


用户配置文件
(detector.yaml)

        ↓

config.cpp
(读取 + 检查 + 转换)

        ↓

AppConfig

        ↓

DetectorConfig

        ↓

Detector运行

*/
