#include "observability_fixture.hpp"
#include <type_traits>
#include "offline_runner.hpp"
#include "input_completion.hpp"
#include "video_exporter.hpp"
#include "run_console.hpp"
#include <sstream>
#include "common/frame_record_reader.hpp"
#include <opencv2/videoio.hpp>
#include <fstream>
using namespace mark;
using namespace observability_fixture;

namespace
{
    // 旧公共接口仍可单独消费；friend不改变PImpl对象布局，内部通道非法顺序主动失败。
    void access()
    {
        static_assert(sizeof(Detector) == sizeof(std::unique_ptr<int>),
                      "public Detector layout changed");
        Detector a(app().detector_config);
        a.config();
        a.reset(ResetReason::External);
        check(a.process(frame()).status == Status::NOT_DETECTED, "legacy consumer failed");
        DetectorDiagnosticsAccess::prepare(a, request());
        rejects(
            [&]
            {
                DetectorDiagnosticsAccess::prepare(a, request());
            });
        a.process(frame(1, 14000));
        auto r = DetectorDiagnosticsAccess::take(a);
        check(r.frame_id == 1 && r.result_status == Status::NOT_DETECTED && r.run_id == "fixture",
              "wrong record");
        rejects(
            [&]
            {
                DetectorDiagnosticsAccess::take(a);
            });
        DetectorDiagnosticsAccess::prepare(a, request());
        a.process(frame(2, 28000));
        rejects(
            [&]
            {
                a.process(frame(3, 42000));
            });
        DetectorDiagnosticsAccess::take(a);
    }

    // 每次非法/NOT_READY也产生记录；非法之后允许低id新段，并保持有效载荷为空。
    void statuses()
    {
        Detector a(app().detector_config);
        DetectorDiagnosticsAccess::prepare(a, request());
        auto bad = frame();
        bad.image.release();
        auto result = a.process(bad);
        auto record = DetectorDiagnosticsAccess::take(a);
        check(result.status == Status::INVALID_INPUT && record.result_status == result.status &&
                  !record.events.empty(),
              "invalid record lost");
        DetectorDiagnosticsAccess::prepare(a, request());
        check(a.process(frame()).status == Status::NOT_DETECTED, "invalid recovery failed");
        DetectorDiagnosticsAccess::take(a);
        auto c = app().detector_config;
        c.temporal.correspondence_uncertainty_px.reset();
        Detector b(c);
        DetectorDiagnosticsAccess::prepare(b, request());
        check(b.process(frame()).status == Status::NOT_READY, "budget gate changed");
        check(DetectorDiagnosticsAccess::take(b).result_status == Status::NOT_READY,
              "notready record lost");
    }

    // 无全局observer；reset A不能污染B，重复外部reset保留每种真实调用数及未知来源。
    void instances()
    {
        Detector a(app().detector_config), b(app().detector_config);
        a.reset(ResetReason::External);
        a.reset(ResetReason::External);
        a.reset(ResetReason::InputChanged);
        a.reset(ResetReason::InvalidSequence);
        DetectorDiagnosticsAccess::prepare(b, request());
        b.process(frame());
        auto rb = DetectorDiagnosticsAccess::take(b);
        auto isReset = [](ReasonCode code)
        {
            return code == ReasonCode::ResetExternal || code == ReasonCode::ResetInputChanged ||
                   code == ReasonCode::ResetInvalidSequence;
        };
        for (const auto &event : rb.events)
            check(!isReset(event.reason), "reset crossed instances");
        DetectorDiagnosticsAccess::prepare(a, request());
        a.process(frame());
        auto ra = DetectorDiagnosticsAccess::take(a);
        uint64_t n = 0;
        for (const auto &event : ra.events)
            if (isReset(event.reason))
            {
                check(!event.source_frame_id, "external reset falsely attributed to input");
                n += event.occurrences;
            }
        check(n == 4, "reset reasons/counts lost or duplicated");
    }

    // 同一真实fixture跨计时与三级诊断逐字段相同；采样不能截断时序历史。
    void consistency()
    {
        std::vector<FrameResult> golden;
        for (bool timing : {false, true})
            for (auto level :
                 {DiagnosticsLevel::Summary, DiagnosticsLevel::Frame, DiagnosticsLevel::Evidence})
            {
                Detector detector(app().detector_config);
                for (uint64_t id = 0; id < 4; ++id)
                {
                    auto req = request(timing);
                    req.level = level;
                    DetectorDiagnosticsAccess::prepare(detector, req);
                    auto result = detector.process(frame(id, id * 14000, id != 2));
                    auto record = DetectorDiagnosticsAccess::take(detector);
                    if (golden.size() < 4)
                        golden.push_back(result);
                    else
                    {
                        const auto &g = golden[id];
                        check(g.status == result.status &&
                                  g.detections.size() == result.detections.size() &&
                                  g.tracks.size() == result.tracks.size() &&
                                  g.display_state.has_value() == result.display_state.has_value(),
                              "I04 status/count changed");
                        for (size_t i = 0; i < g.detections.size(); ++i)
                            check(temporal_fixture::same(g.detections[i], result.detections[i]),
                                  "I04 raw changed");
                        for (size_t i = 0; i < g.tracks.size(); ++i)
                            check(g.tracks[i].detection_index == result.tracks[i].detection_index &&
                                      temporal_fixture::same(g.tracks[i].result,
                                                             result.tracks[i].result),
                                  "I04 stable changed");
                    }
                    check(record.timings[5].status ==
                              (timing ? TimingStatus::MEASURED : TimingStatus::DISABLED),
                          "I04 timing state");
                }
            }
    }

    // baseline冲突必须拒绝；新增范围、类型和无实现开关均校验，不能静默降级。
    void configCases()
    {
        auto a = app();
        a.diagnostics.detail_interval = 0;
        rejects(
            [&]
            {
                validateConfig(a);
            });
        a = app();
        a.diagnostics.detail_first = 9;
        a.diagnostics.detail_last = 8;
        rejects(
            [&]
            {
                validateConfig(a);
            });
        a = app();
        a.offline.export_video = true;
        rejects(
            [&]
            {
                validateConfig(a);
            });
        a.offline.mode = "debug";
        validateConfig(a);
        a = app();
        a.offline.directory = "unused";
        a.detector_config.debug.timing_enabled = false;
        rejects(
            [&]
            {
                runOffline(a, "unused", "unused", ExecutionScope::Full);
            });
    }

    // 真实十帧视频跑完整公共流程，只选2/5/8记录；处理数与结果指纹不受detail采样影响。
    void sampling()
    {
        namespace fs = std::filesystem;
        auto path = fs::temp_directory_path() /
                    ("block5-video-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(path);
        auto video = path / "input.avi";
        cv::VideoWriter writer(video.string(), cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 50,
                               {320, 240});
        check(writer.isOpened(), "I06 fixture codec unavailable");
        for (int i = 0; i < 10; ++i)
            writer.write(cv::Mat(240, 320, CV_8UC3, cv::Scalar(0, 0, 0)));
        writer.release();
        auto config_source =
            std::filesystem::path(__FILE__).parent_path().parent_path() / "config/detector.yaml";
        auto a =
            loadConfig(std::filesystem::relative(config_source, std::filesystem::current_path()));
        a.offline.mode = "baseline";
        a.offline.directory = (path / "baseline").string();
        runOffline(
            a, video.string(),
            (std::filesystem::path(__FILE__).parent_path().parent_path() / "config/detector.yaml")
                .string(),
            ExecutionScope::Full);
        a.offline.mode = "debug";
        a.offline.directory = (path / "debug").string();
        a.diagnostics.level = "frame";
        a.diagnostics.detail_first = 2;
        a.diagnostics.detail_last = 8;
        a.diagnostics.detail_interval = 3;
        runOffline(a, video.string(), "fixture-config", ExecutionScope::Full);
        check(std::filesystem::is_regular_file(loadConfig(path / "baseline/effective_config.yaml")
                                                   .detector_config.marker_geometry_path_),
              "I06 effective relative model reload broken");
        std::ifstream in(path / "debug/frames.jsonl");
        std::string line;
        std::vector<uint64_t> ids;
        while (std::getline(in, line))
            ids.push_back(mark::validation::readJson(line).at("frame_id").u64());
        check(ids == std::vector<uint64_t>{2, 5, 8}, "I06 detail selection changed");
        std::ifstream summary(path / "debug/summary.yaml");
        std::string text((std::istreambuf_iterator<char>(summary)), {});
        check(text.find("submitted: \"10\"") != std::string::npos, "I06 algorithm skipped frames");
        fs::remove_all(path);
    }

    // 四scope都复用真实结果；geometry不冒称检测或公共process，decode不调用稳定层。
    void scopes()
    {
        auto config = app().detector_config;
        auto model = loadMarkerGeometry(config.marker_geometry_path_);
        for (auto scope :
             {ExecutionScope::Geometry, ExecutionScope::Decode, ExecutionScope::Temporal})
        {
            FrameDiagnosticsContext c;
            auto req = request();
            req.scope = scope;
            c.begin(frame(0, 0, true), req);
            auto d = runDecodePipeline(frame(0, 0, true), config, model, &c);
            c.finish();
            check(c.record.timings[5].status == TimingStatus::NOT_EXECUTED,
                  "I07 fake public total");
            if (scope == ExecutionScope::Geometry)
                check(!c.record.result_status && c.record.geometry_scope_result == "READY" &&
                          c.record.counts.completed.has_value(),
                      "I07 geometry result semantics");
            else
                check(d.status == Status::DETECTED, "I07 decode fixture failed");
        }
    }

    void realtime()
    {
        auto r = FrameRecord{};
        check(!r.enqueue_timestamp_ns && !r.enqueue_to_result_us && !r.slot_overwrite_count &&
                  !r.consumed_frame_count && r.realtime_applicability == "not_applicable",
              "I08 fake offline realtime");
    }

    void raw()
    {
        Detector d(app().detector_config);
        DetectorDiagnosticsAccess::prepare(d, request());
        auto result = d.process(frame(0, 0, true));
        auto r = DetectorDiagnosticsAccess::take(d);
        check(r.details && r.details->decoded.detections.size() == result.detections.size() &&
                  temporal_fixture::same(r.details->decoded.detections[0], result.detections[0]),
              "I09 publication channel changed raw");
    }

    void errors()
    {
        auto a = app();
        a.offline.directory =
            (std::filesystem::temp_directory_path() /
             ("block5-missing-" +
              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
                .string();
        rejects(
            [&]
            {
                runOffline(a, "/nonexistent-block5-video.avi", "fixture", ExecutionScope::Full);
            });
        check(!std::filesystem::exists(a.offline.directory),
              "I10 missing input created success run");
    }

    // 视频逐帧导出独立于详情采样；计时关闭也须写十条 DISABLED/null 完成记录。
    void video_sampling()
    {
        namespace fs = std::filesystem;
        auto path = fs::temp_directory_path() /
                    ("final-fixes-video-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(path);
        auto video = path / "input.avi";
        cv::VideoWriter writer(video.string(), cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 50,
                               {320, 240});
        check(writer.isOpened(), "video fixture codec unavailable");
        for (int i = 0; i < 10; ++i)
            writer.write(cv::Mat(240, 320, CV_8UC3, cv::Scalar(0, 0, 0)));
        writer.release();
        for (bool enabled : {false, true})
        {
            auto a = app();
            a.offline.mode = "debug";
            a.offline.export_video = true;
            a.offline.directory = (path / (enabled ? "on" : "off")).string();
            a.detector_config.debug.timing_enabled = enabled;
            a.diagnostics.level = "frame";
            a.diagnostics.detail_first = 2;
            a.diagnostics.detail_last = 8;
            a.diagnostics.detail_interval = 3;
            check(runOffline(a, video.string(), "fixture-config", ExecutionScope::Full) == 0,
                  "sampled video run failed");
            std::ifstream side(fs::path(a.offline.directory) / "export_timings.jsonl");
            std::string line;
            uint64_t count = 0, chosen = 0;
            while (std::getline(side, line))
            {
                auto row = mark::validation::readJson(line);
                check(row.at("frame_id").u64() == count, "video export skipped frames");
                check(row.at("status").text == (enabled ? "MEASURED" : "DISABLED") &&
                          row.at("elapsed_us").null() == !enabled,
                      "video timing ignores switch");
                chosen += row.at("selected_record").boolean;
                ++count;
            }
            check(count == 10 && chosen == 3, "sampled/video export counts mismatch");
            cv::VideoCapture reader((fs::path(a.offline.directory) / "overlay.mp4").string());
            cv::Mat image;
            count = 0;
            while (reader.read(image))
            {
                check(image.size() == cv::Size(320, 240), "overlay dimensions changed");
                ++count;
            }
            check(count == 10, "overlay not fully readable");
        }
        VideoExporter partial;
        partial.open(path, {320, 240}, 50, "MJPG");
        rejects(
            [&]
            {
                partial.write(cv::Mat(10, 10, CV_8UC3));
            });
        partial.write(cv::Mat(240, 320, CV_8UC3, cv::Scalar(0, 0, 0)));
        partial.finish(false);
        check(fs::exists(path / "overlay.partial.mp4") && !fs::exists(path / "overlay.mp4"),
              "incomplete video published");
        rejects(
            [&]
            {
                partial.finish(true);
            });
        fs::remove_all(path);
    }

    // 新工程参数必须往返且非法值拒绝；旧全局未实现断言改为模式/值域约束。
    void output_config()
    {
        auto a = app();
        a.offline.mode = "debug";
        a.offline.export_video = true;
        a.offline.expected_frame_count = UINT64_MAX;
        a.offline.display_probe_timeout_ms = 45;
        auto path = std::filesystem::temp_directory_path() /
                    ("final-fixes-config-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        writeEffectiveConfig(a, path);
        auto b = loadConfig(path);
        check(b.offline.export_video && b.offline.expected_frame_count == UINT64_MAX &&
                  b.offline.video_fourcc == "MJPG" && b.offline.video_filename == "overlay.mp4" &&
                  b.offline.display_probe_timeout_ms == 45,
              "output config roundtrip lost");
        std::filesystem::remove(path);
        a.offline.video_fourcc = "mp4v";
        rejects(
            [&]
            {
                validateConfig(a);
            });
        a.offline.video_fourcc = "MJPG";
        a.offline.video_filename = "../overlay.mp4";
        rejects(
            [&]
            {
                validateConfig(a);
            });
        a.offline.video_filename = "overlay.mp4";
        a.offline.display_probe_timeout_ms = 0;
        rejects(
            [&]
            {
                validateConfig(a);
            });
    }

    // 终端只展示实际统计；导出/等待即使更慢也不能成为算法瓶颈，关闭计时不造 0ms。
    void console_contract()
    {
        RunMetadata metadata;
        metadata.run_id = "console-fixture";
        metadata.environment = {{"coverage", "full_input"}, {"completion_reason", "COMPLETE"}};
        RunSummary summary;
        summary.submitted = 1;
        summary.result_fingerprint = "0000000000000001";
        summary.statuses["2"] = 1;
        summary.durations[1] = {std::chrono::nanoseconds(10)};
        summary.durations[2] = {std::chrono::nanoseconds(10)};
        summary.durations[8] = {std::chrono::nanoseconds(10000)};
        std::ostringstream on, off, failed;
        write_run_console(on, metadata, summary, 1, true, ExecutionScope::Geometry);
        check(on.str().find("瓶颈阶段（已测样本平均值）：预处理") != std::string::npos,
              "bottleneck scope/tie wrong");
        write_run_console(off, metadata, summary, 1, false, ExecutionScope::Full);
        check(off.str().find("阶段计时未启用；瓶颈阶段不可判定") != std::string::npos &&
                  off.str().find("0.000 ms") == std::string::npos,
              "disabled console fakes time");
        write_failure_console(failed, "zero-frame run");
        check(failed.str().find("run=") == std::string::npos,
              "zero input created technical success line");
        check(reason_label("unrecognized") == "未映射原因", "unknown reason guessed");
        std::ostringstream unknown;
        write_failure_console(unknown, "unrecognized video subsystem error");
        check(unknown.str().find("未映射原因") != std::string::npos &&
                  unknown.str().find("unrecognized video subsystem error") != std::string::npos,
              "unknown exception guessed or technical detail lost");
    }

    // 原 Geometry scope 无谓依赖 corner；同一真实输入只切换该预算，完整/Decode 仍未就绪。
    void geometry_budget_isolation()
    {
        auto config = app().detector_config;
        auto model = fixture::model();
        config.corner_.observation_budget_.reset();
        for (auto scope : {ExecutionScope::Geometry, ExecutionScope::Decode, ExecutionScope::Full})
        {
            FrameDiagnosticsContext context;
            auto req = request();
            req.scope = scope;
            auto input = frame(0, 0, true);
            context.begin(input, req);
            auto result = runDecodePipeline(input, config, model, &context);
            context.finish();
            if (scope == ExecutionScope::Geometry)
            {
                check(context.record.geometry_scope_result == "READY" &&
                          context.record.counts.completed.has_value(),
                      "geometry corner gate remains");
                check(context.record.timings[3].status == TimingStatus::NOT_EXECUTED &&
                          context.record.timings[4].status == TimingStatus::NOT_EXECUTED &&
                          context.record.timings[5].status == TimingStatus::NOT_EXECUTED,
                      "geometry faked later stages");
            }
            else
                check(result.status == Status::NOT_READY, "missing corner budget passed decode");
        }
        Detector public_detector(config);
        check(public_detector.process(frame(0, 0, true)).status == Status::NOT_READY,
              "public budget gate bypassed");
        config.corner_ = app().detector_config.corner_;
        check(runDecodePipeline(frame(0, 0, true), config, model).status == Status::DETECTED,
              "restored budget changed result");
    }

    // 旧 EOF 没有独立边界断言；完整、提前 EOF、用户停止及子集分别核实。
    void input_completion()
    {
        check(!evaluate_input_completion(1676, 1676, false, false, {}).incomplete,
              "full input rejected");
        check(evaluate_input_completion(41, 1676, false, false, {}).incomplete,
              "truncation accepted");
        check(evaluate_input_completion(1677, 1676, false, false, {}).incomplete,
              "extra frames accepted");
        check(evaluate_input_completion(10, std::nullopt, false, false, {}).reason ==
                  "COVERAGE_UNVERIFIED",
              "unknown coverage accepted");
        check(evaluate_input_completion(10, 10, true, false, {}).reason == "USER_STOP",
              "user stop accepted");
        check(!evaluate_input_completion(6, 10, false, true, {}).incomplete,
              "valid subset rejected");
        check(evaluate_input_completion(6, 10, false, true, {9}).incomplete,
              "missing subset accepted");
        for (double n : {0., -1., 1.5, std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity(), std::ldexp(1., 64)})
            check(!metadata_frame_count(n), "invalid metadata accepted");
        check(metadata_frame_count(1676) == 1676, "valid metadata rejected");
    }

}

int main()
{
    return temporal_fixture::run({{"F12_video_sampling", video_sampling},
                                  {"F13_console", console_contract},
                                  {"F11_output_config", output_config},
                                  {"F06_geometry_budget", geometry_budget_isolation},
                                  {"F01_input_completion", input_completion},
                                  {"I01_internal_access", access},
                                  {"I02_early_status", statuses},
                                  {"I03_R07_reset_isolation", instances},
                                  {"I04_modes", consistency},
                                  {"I05_config", configCases},
                                  {"I06_sampling", sampling},
                                  {"I07_scopes", scopes},
                                  {"I08_offline", realtime},
                                  {"I09_raw", raw},
                                  {"I10_io", errors}});
}
