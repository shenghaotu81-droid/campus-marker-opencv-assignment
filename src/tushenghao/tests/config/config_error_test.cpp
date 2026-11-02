// Q02/C01：配置与标定坏输入主动检查；本次临时输入由显式 test_root 隔离。
#include "detector/config.hpp"
#include "reader/config_error.hpp"
#include "input/calibration_config.hpp"
#include "results/calibration_result.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <limits>
#include <cmath>
#include <chrono>

namespace
{

    void writeTestConfig(const std::filesystem::path &path, const std::string &content)
    {
        std::ofstream file(path);

        if (!file)
        {
            throw std::runtime_error("cannot create test config file");
        }

        file << "%YAML:1.0\n"; // OpenCV FileStorage 要求版本头。

        file << content;
    }

    // 测试配置文件不存在时是否抛出 ConfigError。
    bool testMissingFile(const std::filesystem::path &test_root)
    {
        try
        {
            mark::loadConfig(test_root / "not_exist_config.yaml");
        }
        catch (const mark::ConfigError &e)
        {
            std::cout << "[expected] " << e.what() << std::endl;

            // 期望：文件不存在时统一抛出 ConfigError。
            return true;
        }

        return false;
    }

    // 测试 detector.mode 类型错误时是否抛出 ConfigError。
    bool testModeTypeError(const std::filesystem::path &test_root)
    {
        const auto path = test_root / "mode_type_error.yaml";

        writeTestConfig(path,
                        R"(
schema_version: 1
input:
  pixel_format: BGR8
  timestamp_unit: us
preprocess:
  work_width: 640
  work_height: 480
  threshold: 128
detector:
  mode: 123
temporal:
  stabilization_enabled: 0
  display_hold_enabled: 0
  max_hold_frames: 5
display:
  show_window: 0
  show_held_state: 0
  draw_candidates: 0
output:
  run_mode: baseline
debug:
  timing_enabled: 0
)");

        try
        {
            mark::loadConfig(path);
        }

        catch (const mark::ConfigError &e)
        {
            std::cout << "[expected] " << e.what() << std::endl;

            // 期望：detector.mode 类型不是字符串时抛出 ConfigError。
            std::filesystem::remove(path);
            return true;
        }

        std::filesystem::remove(path);
        return false;
    }

    // 测试 detector.mode 非法值时是否抛出 ConfigError。
    bool testModeValueError(const std::filesystem::path &test_root)
    {
        const auto path = test_root / "mode_value_error.yaml";

        writeTestConfig(path,
                        R"(
schema_version: 1
input:
  pixel_format: BGR8
  timestamp_unit: us
preprocess:
  work_width: 640
  work_height: 480
  threshold: 128
detector:
  mode: invalid_mode
temporal:
  stabilization_enabled: 0
  display_hold_enabled: 0
  max_hold_frames: 5
display:
  show_window: 0
  show_held_state: 0
  draw_candidates: 0
output:
  run_mode: baseline
debug:
  timing_enabled: 0
)");

        try
        {
            mark::loadConfig(path);
        }
        catch (const mark::ConfigError &e)
        {
            std::cout << "[expected] " << e.what() << std::endl;

            // 期望：detector.mode 不支持未知模式时抛出 ConfigError。
            std::filesystem::remove(path);
            return true;
        }

        std::filesystem::remove(path);
        return false;
    }

    // 测试 preprocess.threshold 超出范围时是否抛出 ConfigError。
    bool testThresholdRangeError(const std::filesystem::path &test_root)
    {
        const auto path = test_root / "threshold_error.yaml";

        writeTestConfig(path,
                        R"(
schema_version: 1
input:
  pixel_format: BGR8
  timestamp_unit: us
preprocess:
  work_width: 640
  work_height: 480
  threshold: 300
detector:
  mode: SegmentedMark
temporal:
  stabilization_enabled: 0
  display_hold_enabled: 0
  max_hold_frames: 5
display:
  show_window: 0
  show_held_state: 0
  draw_candidates: 0
output:
  run_mode: baseline
debug:
  timing_enabled: 0
)");

        try
        {
            mark::loadConfig(path);
        }
        catch (const mark::ConfigError &e)
        {
            std::cout << "[expected] " << e.what() << std::endl;

            // 期望：threshold 不在 0~255 范围时抛出 ConfigError。
            std::filesystem::remove(path);
            return true;
        }

        std::filesystem::remove(path);
        return false;
    }

    // 计时已经实现，旧负例改测仍未实现的视频导出，不删除未实现功能拒绝覆盖。
    bool testUnsupportedFeatureError(const std::filesystem::path &test_root)
    {
        const auto path = test_root / "unsupported_feature.yaml";

        writeTestConfig(path,
                        R"(
schema_version: 1
input:
  pixel_format: BGR8
  timestamp_unit: us
preprocess:
  work_width: 640
  work_height: 480
  threshold: 128
detector:
  mode: SegmentedMark
temporal:
  stabilization_enabled: 0
  display_hold_enabled: 0
  max_hold_frames: 5
display:
  show_window: 0
  show_held_state: 0
  draw_candidates: 0
output:
  run_mode: baseline
  export_video: 1
debug:
  timing_enabled: 1
)");

        try
        {
            mark::loadConfig(path);
        }
        catch (const mark::ConfigError &e)
        {
            std::cout << "[expected] " << e.what() << std::endl;

            // 期望：未实现功能开启时抛出 ConfigError。
            std::filesystem::remove(path);
            return true;
        }

        std::filesystem::remove(path);
        return false;
    }

    // 新增时序字段同时测试直接配置与YAML类型/缺失/重复/溢出，防止Release空绿。
    bool temporalErrors(const std::filesystem::path &test_root)
    {
        auto base = mark::loadConfig(std::filesystem::path(VISION_SOURCE_ROOT) /
                                     "config/detector.yaml");
        // 缺预算兼容测试是独立fixture，生产获批数值不能使负例失去覆盖。
        base.detector_config.temporal.correspondence_uncertainty_px.reset();
        base.detector_config.temporal.max_smoothing_deviation_px.reset();
        double mark::TemporalConfig::*fields[] = {
            &mark::TemporalConfig::reference_dt_ms,
            &mark::TemporalConfig::reference_alpha,
            &mark::TemporalConfig::history_max_gap_ms,
            &mark::TemporalConfig::max_center_distance_diagonal_ratio,
            &mark::TemporalConfig::min_area_ratio,
            &mark::TemporalConfig::max_area_ratio};
        for (auto field : fields)
            for (double v : {0., -1., std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity()})
            {
                auto app = base;
                app.detector_config.temporal.*field = v;
                bool bad = false;
                try
                {
                    mark::validateConfig(app);
                }
                catch (const mark::ConfigError &)
                {
                    bad = true;
                }
                if (!bad)
                {
                    std::cerr << "temporal rejection missing at line " << __LINE__ << "\n";
                    return false;
                }
            }
        for (int variant = 0; variant < 8; ++variant)
        {
            auto app = base;
            auto &t = app.detector_config.temporal;
            if (variant == 0)
                t.reference_alpha = 1;
            if (variant == 1)
                t.min_area_ratio = 1.1;
            if (variant == 2)
                t.max_area_ratio = .9;
            if (variant == 3)
                t.correspondence_uncertainty_px = -1;
            if (variant == 4)
                t.correspondence_uncertainty_px = std::numeric_limits<double>::quiet_NaN();
            if (variant == 5)
                t.max_smoothing_deviation_px = 0;
            if (variant == 6)
                t.max_smoothing_deviation_px = std::numeric_limits<double>::infinity();
            if (variant == 7)
                t.max_hold_frames = -1;
            bool bad = false;
            try
            {
                mark::validateConfig(app);
            }
            catch (const mark::ConfigError &)
            {
                bad = true;
            }
            if (!bad)
            {
                std::cerr << "temporal rejection missing at line " << __LINE__ << "\n";
                return false;
            }
        }
        auto path = test_root / "block4-temporal-errors.yaml";
        mark::writeEffectiveConfig(base, path);
        std::ifstream input(path);
        std::string text((std::istreambuf_iterator<char>(input)), {});
        input.close();
        for (const std::string replacement :
             {"reference_dt_ms: \"bad\"", "reference_dt_ms: .Nan",
              "reference_dt_ms: 14.\n   reference_dt_ms: 14.",
              "correspondence_uncertainty_px: \"\"", "max_smoothing_deviation_px: -1",
              "misspelled_budget: 2"})
        {
            auto copy = text;
            auto start = copy.find("reference_dt_ms:");
            auto end = copy.find('\n', start);
            copy.replace(start, end - start, replacement);
            {
                std::ofstream out(path);
                out << copy;
            }
            bool bad = false;
            try
            {
                mark::loadConfig(path);
            }
            catch (const std::exception &)
            {
                bad = true;
            }
            if (!bad)
            {
                std::cerr << "temporal rejection missing at line " << __LINE__ << "\n";
                return false;
            }
        }
        for (const std::string value : {"5.5", "2147483648", "4294967296"})
        {
            auto copy = text;
            auto start = copy.find("max_hold_frames:");
            auto end = copy.find('\n', start);
            copy.replace(start, end - start, "max_hold_frames: " + value);
            {
                std::ofstream out(path);
                out << copy;
            }
            bool bad = false;
            try
            {
                mark::loadConfig(path);
            }
            catch (const std::exception &)
            {
                bad = true;
            }
            if (!bad)
            {
                std::cerr << "temporal rejection missing at line " << __LINE__ << "\n";
                return false;
            }
        }
        // 旧schema=1省略六个冻结字段仍加载，G-B不得被补默认；旧三键仍必填。
        for (const std::string field :
             {"reference_dt_ms", "reference_alpha", "history_max_gap_ms",
              "max_center_distance_diagonal_ratio", "min_area_ratio", "max_area_ratio"})
        {
            auto start = text.find("   " + field + ":", text.find("temporal:"));
            if (start != std::string::npos)
                text.erase(start, text.find('\n', start) - start + 1);
        }
        {
            std::ofstream out(path);
            out << text;
        }
        auto loaded = mark::loadConfig(path).detector_config.temporal;
        if (loaded.reference_dt_ms != 14 || loaded.reference_alpha != .7 ||
            loaded.correspondence_uncertainty_px || loaded.max_smoothing_deviation_px)
            return false;
        for (const std::string field :
             {"stabilization_enabled", "display_hold_enabled", "max_hold_frames"})
        {
            auto copy = text;
            auto start = copy.find("   " + field + ":");
            copy.erase(start, copy.find('\n', start) - start + 1);
            {
                std::ofstream out(path);
                out << copy;
            }
            bool bad = false;
            try
            {
                mark::loadConfig(path);
            }
            catch (const std::exception &)
            {
                bad = true;
            }
            if (!bad)
            {
                std::cerr << "temporal rejection missing at line " << __LINE__ << "\n";
                return false;
            }
        }
        std::filesystem::remove(path);
        return true;
    }

} // namespace

namespace
{
    // C04/C05：成功/失败交错时保留源身份；不同尺寸由视频信息显式交给求解与写出。
    bool calibrationFlow(const std::filesystem::path &test_root)
    {
        CalibrationConfig config; initialize_default_config(config);
        cv::Mat board(480, 640, CV_8UC3, cv::Scalar(255,255,255));
        for (int y = 0; y < 7; ++y) for (int x = 0; x < 7; ++x)
            cv::circle(board, {120+x*50, 70+y*50}, 7, {0,0,0}, -1);
        CalibrationDetectionResult detected;
        if (!detect_calibration_points(config, {{7,board},{19,cv::Mat::zeros(board.size(), CV_8UC3)},{42,board}}, detected) ||
            detected.observations.size()!=3 || !detected.observations[0].detected ||
            detected.observations[1].detected || !detected.observations[2].detected ||
            detected.observations[1].frame_id!=19) return false;
        for (const auto size : {cv::Size(320,240),cv::Size(640,480)})
        {
            CalibrationDetectionResult samples; samples.object_points=detected.object_points;
            const cv::Mat camera=(cv::Mat_<double>(3,3)<<size.width,0,size.width/2.,0,size.width,size.height/2.,0,0,1);
            for (int i=0;i<6;++i)
            {
                std::vector<cv::Point2f> points;
                cv::projectPoints(samples.object_points,cv::Vec3d(.04*i,.1-.02*i,.03*i),
                                  cv::Vec3d(-.05+.01*i,-.04,.7+.03*i),camera,cv::Mat(),points);
                samples.observations.push_back({10+i*3,points,true});
                samples.observations.push_back({11+i*3,{},false});
            }
            CalibrationResult result;
            CalibrationVideoInfo video; video.width=size.width;video.height=size.height;video.source_path="fixture";
            if (!run_camera_calibration(video,samples,result) || result.image_size!=size ||
                result.source_path!="fixture" || result.frame_ids!=std::vector<int>({10,13,16,19,22,25}) ||
                !std::isfinite(result.reprojection_error) || result.reprojection_error>1e-3) return false;
            config.output_path=(test_root/"lean-calibration-roundtrip.yaml").string();
            if (!save_calibration_result(config,result)) return false;
            cv::FileStorage stored(config.output_path,cv::FileStorage::READ);
            if (int(stored["image_width"])!=size.width || int(stored["image_height"])!=size.height) return false;
            stored.release(); std::filesystem::remove(config.output_path);
        }
        return true;
    }

    // Q02：先确认完整新协议可读，再逐项注入旧键，避免缺字段负例掩盖兼容残留。
    bool rejectedOldProtocol(const std::filesystem::path &test_root)
    {
        const auto path = test_root / "lean-old-protocol.yaml";
        auto config = mark::loadConfig(std::filesystem::path(VISION_SOURCE_ROOT) /
                                       "config/detector.yaml");
        mark::writeEffectiveConfig(config, path);
        std::ifstream input(path);
        std::ostringstream buffer;
        buffer << input.rdbuf();
        const auto valid = buffer.str();
        mark::loadConfig(path);
        const std::pair<std::string, std::string> cases[] = {
            {"   corner:", "      local_search_margin_ratio: 0.2"},
            {"   corner:", "      reject_truncated_corner: 0"},
            {"   corner:", "      edge_point_distance_threshold: 3."},
            {"output:", "   show_window: 0"},
            {"output:", "   show_held_state: 0"},
            {"debug:", "   draw_candidates: 0"},
            {"SegmentedMark", "Skeleton"},
            {"SegmentedMark", "skeleton"}};
        for (const auto &[anchor, old] : cases)
        {
            auto bad = valid;
            const auto at = bad.find(anchor);
            if (at == std::string::npos) throw std::runtime_error("fixture anchor absent: " + anchor);
            if (anchor == "SegmentedMark") bad.replace(at, anchor.size(), old);
            else bad.insert(at + anchor.size(), "\n" + old);
            { std::ofstream output(path); output << bad; }
            bool rejected = false;
            try { mark::loadConfig(path); }
            catch (const mark::ConfigError &error)
            {
                const std::string message = error.what();
                rejected = anchor == "SegmentedMark" ? message.find("mode") != std::string::npos
                                                      : message.find(old.substr(0, old.find(':')).substr(old.find_first_not_of(' '))) != std::string::npos;
            }
            if (!rejected) { std::cerr << "old protocol accepted/wrong error: " << old << '\n'; return false; }
        }
        std::filesystem::remove(path);
        return true;
    }

    // C01 负例：在打开视频前核验字段类型/完整 CLI 和 NaN，合法 YAML→CLI 覆盖不变。
    bool calibrationConfigErrors(const std::filesystem::path &test_root)
    {
        const auto path = test_root / "lean-calibration-errors.yaml";
        const std::string valid =
            "schema_version: 1\nvideo_path: fixture.avi\noutput_path: result.yaml\n"
            "board_rows: 7\nboard_cols: 7\npoint_spacing: 0.03\nframe_list: [0, 2, 4]\n";
        writeTestConfig(path, valid);
        CalibrationConfig config;
        initialize_default_config(config);
        if (!load_config_yaml(path.string(), config) || !validate_config(config) ||
            config.frame_list != std::vector<int>{0, 2, 4})
            return false;

        for (const std::string bad : {
                 "video_path: fixture.avi\n", "schema_version: \"1\"\n",
                 "schema_version: 1\nunknown_field: 1\n",
                 "schema_version: 1\nboard_rows: 7\nboard_rows: 8\n",
                 "schema_version: 1\nboard_rows: 7.5\n",
                 "schema_version: 4294967297\n",
                 "schema_version: 1\nboard_rows: 4294967297\n",
                 "schema_version: 1\nboard_cols: 0x100000001\n",
                 "schema_version: 1\nstart_frame: 0\n",
                 "schema_version: 1\nstart_frame: 4294967296\n",
                 "schema_version: 1\nend_frame: -1\n",
                 "schema_version: 1\nend_frame: 4294967295\n",
                 "schema_version: 1\nboard_cols: \"7\"\n",
                 "schema_version: 1\nvideo_path: 7\n",
                 "schema_version: 1\noutput_path: []\n",
                 "schema_version: 1\npoint_spacing: .Nan\n",
                 "schema_version: 1\npoint_spacing: .Inf\n",
                 "schema_version: 1\npoint_spacing: \"0.03\"\n",
                 "schema_version: 1\nframe_list: 7\n",
                 "schema_version: 1\nframe_list: {}\n",
                 "schema_version: 1\nframe_list: [0, 1.5]\n",
                 "schema_version: 1\nframe_list: [\"1\"]\n",
                 "schema_version: 1\nframe_list: [-1]\n",
                 "schema_version: 1\nframe_list: [0, 4294967297]\n",
                 "schema_version: 1\nframe_list:\n  - 0\n  - 4294967297\n"})
        {
            writeTestConfig(path, bad);
            CalibrationConfig rejected;
            initialize_default_config(rejected);
            if (load_config_yaml(path.string(), rejected))
            {
                std::cerr << "calibration YAML accepted: " << bad;
                std::filesystem::remove(path);
                return false;
            }
        }
        std::filesystem::remove(path);

        auto parse = [](std::vector<std::string> arguments, CalibrationConfig &result)
        {
            std::vector<char *> argv;
            for (auto &argument : arguments) argv.push_back(argument.data());
            std::string config_path;
            if (!extract_config_path(static_cast<int>(argv.size()), argv.data(), config_path))
                return false;
            apply_cli_overrides(static_cast<int>(argv.size()), argv.data(), result);
            return validate_config(result);
        };
        if (!parse({"calibration_tool", "--config", "fixture.yaml", "--rows", "8",
                    "--cols", "9", "--spacing", "0.04", "--video", "override.avi"}, config) ||
            config.board_rows != 8 || config.board_cols != 9 ||
            config.point_spacing != 0.04 || config.video_path != "override.avi")
            return false;

        for (const auto &suffix : std::vector<std::vector<std::string>>{
                 {"--config", "again.yaml"}, {"--unknown", "1"}, {"--rows"},
                 {"--start-frame", "0"}, {"--end-frame", "-1"},
                 {"--spacing", "--cols", "7"}, {"--rows", "7", "--rows", "8"},
                 {"--rows", "7junk"}, {"--rows", "4294967296"},
                 {"--spacing", "nan"}, {"--spacing", "inf"}, {"--spacing", "0.04junk"},
                 {"--spacing", "0"}, {"--spacing", "-1"}})
        {
            auto arguments = std::vector<std::string>{"calibration_tool", "--config", "fixture.yaml"};
            arguments.insert(arguments.end(), suffix.begin(), suffix.end());
            auto rejected = config;
            try
            {
                if (!parse(arguments, rejected)) continue;
            }
            catch (const std::exception &) { continue; }
            std::cerr << "calibration CLI accepted invalid case\n";
            return false;
        }
        for (double value : {std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity(), -1., 0.})
        {
            config.point_spacing = value;
            if (validate_config(config)) return false;
        }
        std::cout << "PASS calibration strict YAML/CLI/spacing\n";
        return true;
    }
}

int main()
{
    try
    {
        // V06/F71：跨构建可并行运行；目录创建成功才取得所有权，异常也清理本次文件。
        struct RunDirectory
        {
            std::filesystem::path root;
            ~RunDirectory() { std::error_code ignored; std::filesystem::remove_all(root,ignored); }
        };
        const auto base=std::filesystem::temp_directory_path() /
            ("lean-config-test-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
        std::filesystem::path root;
        for(uint64_t attempt=0;;++attempt)
        {
            auto candidate=std::filesystem::path(base.string()+"-"+std::to_string(attempt));
            std::error_code error;
            if(std::filesystem::create_directory(candidate,error)) { root=std::move(candidate); break; }
            if(error)throw std::runtime_error("cannot create test directory: "+error.message());
        }
        const RunDirectory run{root};
        if (!calibrationFlow(run.root)) return 1;
        if (!rejectedOldProtocol(run.root)) return 1;
        if (!calibrationConfigErrors(run.root)) return 1;
        if (!temporalErrors(run.root))
        {
            std::cerr << "temporal errors failed\n";
            return 1;
        }
        std::cout << "run testMissingFile"
                  << std::endl;
        if (!testMissingFile(run.root))
        {
            return 1;
        }
        std::cout << "run testModeTypeError" << std::endl;
        if (!testModeTypeError(run.root))
        {
            return 1;
        }
        std::cout << "run testModeValueError" << std::endl;
        if (!testModeValueError(run.root))
        {
            return 1;
        }
        std::cout << "run testThresholdRangeError" << std::endl;
        if (!testThresholdRangeError(run.root))
        {
            return 1;
        }
        std::cout << "run testUnsupportedFeatureError" << std::endl;
        if (!testUnsupportedFeatureError(run.root))
        {
            return 1;
        }
    }
    catch (...)
    {
        return 1;
    }

    return 0;
}
