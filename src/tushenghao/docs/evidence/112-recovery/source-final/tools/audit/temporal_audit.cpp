// 原video与480x32实验网格保留，统一FrameRecord/recorder；阶段装配不假称公共process。
#include "offline_runner.hpp"
#include "pipeline/stabilize_stage.hpp"
#include "pipeline/diagnostics_context.hpp"
#include "corners/screen_order.hpp"
#include "common/file_digest.hpp"
#include <iostream>
#include <map>
#include <sstream>

namespace
{
    using namespace mark;

    // 合成测量仍采用原物理环float规范序，不改网格或对应预算实验规则。
    Detection measured(const std::array<cv::Point2d, 4> &physical, bool known)
    {
        auto rounded = physical;
        for (auto &p : rounded)
            p = cv::Point2f(p);
        std::string why;
        auto order = orderScreenCycle(rounded, {}, why);
        if (!order)
            throw std::runtime_error("EXPERIMENT_INPUT_INVALID: " + why);
        Detection d{};
        for (size_t i = 0; i < 4; ++i)
            d.corners[i] = order->screen_points[i];
        d.bbox = boundingBoxFromCorners(d.corners);
        if (known)
            d.attributes.orientation = order->input_to_screen;
        return d;
    }

    int experiment(AppConfig app, const std::string &config_path, const std::string &noise_path)
    {
        std::ifstream in(noise_path);
        if (!in)
            throw std::runtime_error("cannot open noise CSV");
        std::string line;
        std::getline(in, line);
        std::vector<std::array<cv::Point2d, 4>> noise;
        while (std::getline(in, line))
        {
            std::istringstream s(line);
            std::string v;
            std::getline(s, v, ',');
            std::array<cv::Point2d, 4> n{};
            for (auto &p : n)
            {
                if (!std::getline(s, v, ','))
                    throw std::runtime_error("noise columns missing");
                p.x = std::stod(v);
                if (!std::getline(s, v, ','))
                    throw std::runtime_error("noise columns missing");
                p.y = std::stod(v);
                if (!std::isfinite(p.x) || !std::isfinite(p.y))
                    throw std::runtime_error("noise nonfinite");
            }
            noise.push_back(n);
        }
        if (noise.size() < 32)
            throw std::runtime_error("requires >=32 recorded C errors");
        if (!app.detector_config.temporal.correspondence_uncertainty_px)
            throw std::runtime_error("experiment r missing");
        app.offline.mode = "debug";
        app.diagnostics.level = "evidence";
        RunMetadata m;
        m.run_id = std::filesystem::path(app.offline.directory).filename().string();
        m.mode = "debug";
        m.run_purpose = "experiment-grid";
        m.config_path = std::filesystem::absolute(config_path).string();
        m.model_path =
            std::filesystem::absolute(app.detector_config.marker_geometry_path_).string();
        app.detector_config.marker_geometry_path_ = m.model_path;
        m.source_path = std::filesystem::absolute(noise_path).string();
        m.input_fingerprint = "SHA256:" + audit::sha256(noise_path);
        m.code_fingerprint = MARK_CODE_HASH;
        m.commit_label = MARK_COMMIT;
        m.environment = {{"execution_scope", "temporal"},
                         {"experimental", "true"},
                         {"noise_sha256", audit::sha256(noise_path)},
                         {"config_sha256", audit::sha256(config_path)},
                         {"model_sha256", audit::sha256(app.detector_config.marker_geometry_path_)},
                         {"CPU", "UNKNOWN: experiment tool does not collect CPU metadata"}};
        DiagnosticsRecorder recorder(app.diagnostics);
        recorder.beginRun(m, app.offline.directory);
        writeEffectiveConfig(app, recorder.directory() / "effective_config.yaml");
        uint64_t segment = 0, id = 0;
        ResultFingerprint fingerprint;
        for (int dt : {7, 14, 28})
            for (double budget : {.5, 1., 2., 4.})
                for (bool known : {false, true})
                    for (bool noisy : {false, true})
                        for (int mode : {0, 1})
                        {
                            std::vector<double> rates =
                                mode ? std::vector<double>{0, .5, 2, 5}
                                     : std::vector<double>{0, .5, 1, 2, 4, 8};
                            for (double rate : rates)
                            {
                                auto c = app.detector_config.temporal;
                                c.stabilization_enabled = true;
                                c.max_smoothing_deviation_px = budget;
                                TemporalStabilizer t(c);
                                for (int n = 0; n < 32; ++n)
                                {
                                    double angle = mode ? n * rate * CV_PI / 180 : 0,
                                           dx = mode ? 0 : n * rate, cs = std::cos(angle),
                                           sn = std::sin(angle);
                                    std::array<cv::Point2d, 4> truth{
                                        {{-50, -50}, {50, -50}, {50, 50}, {-50, 50}}};
                                    for (auto &p : truth)
                                        p = {720 + dx + cs * p.x - sn * p.y,
                                             540 + sn * p.x + cs * p.y};
                                    auto p = truth;
                                    if (noisy)
                                        for (size_t i = 0; i < 4; ++i)
                                            p[i] += noise[n][i];
                                    auto raw = measured(p, known);
                                    FrameInput input{cv::Mat(), id, int64_t(n * dt * 1000),
                                                     TimestampSource::Unknown};
                                    DiagnosticsRequest request;
                                    request.run_id = m.run_id;
                                    request.scope = ExecutionScope::Temporal;
                                    request.timing_enabled = true;
                                    request.selected = true;
                                    request.level = DiagnosticsLevel::Evidence;
                                    request.timestamp_recipe = "synthetic dt_ms";
                                    FrameDiagnosticsContext context;
                                    context.begin(input, request);
                                    context.record.original_size = {1440, 1080};
                                    for (auto stage :
                                         {Stage::Preprocess, Stage::Detect, Stage::Decode})
                                        context.timing.markStatus(stage,
                                                                  TimingStatus::NOT_EXECUTED);
                                    FrameResult r{};
                                    {
                                        ScopedStageTimer timer(context.timing, Stage::Stabilize);
                                        auto result = t.update(
                                            {raw},
                                            {id, input.timestamp_us, TimestampSource::Unknown},
                                            {1440, 1080});
                                        r = {id,
                                             input.timestamp_us,
                                             result.status,
                                             {raw},
                                             result.tracks,
                                             std::nullopt,
                                             {result.diagnostics.association.reason,
                                              result.diagnostics.reset_or_fallback_reason}};
                                        context.details->temporal = result.diagnostics;
                                        ExperimentalDetails detail;
                                        detail.segment = segment;
                                        detail.sample = n;
                                        detail.dt_ms = dt;
                                        detail.mode = mode;
                                        detail.rate = rate;
                                        detail.r_px = *c.correspondence_uncertainty_px;
                                        detail.deviation_px = budget;
                                        detail.known = known;
                                        detail.noisy = noisy;
                                        detail.truth_physical = truth;
                                        detail.raw_physical = p;
                                        for (auto &point : detail.raw_physical)
                                            point = cv::Point2f(point);
                                        context.details->experimental = detail;
                                        context.output(r);
                                        for (const auto &reason : r.diagnostics)
                                            if (!reason.empty())
                                                context.event(Stage::Stabilize,
                                                              ReasonCode::OtherRecordedReason,
                                                              reason);
                                    }
                                    context.finish();
                                    fingerprint.add(r);
                                    auto start = std::chrono::steady_clock::now();
                                    recorder.submit(context.record);
                                    recorder.addExportTiming(id, std::chrono::steady_clock::now() -
                                                                     start);
                                    ++id;
                                }
                                ++segment;
                            }
                        }
        recorder.summary().result_fingerprint = fingerprint.hex();
        recorder.summary().fingerprint_frames = id;
        auto s = recorder.finishRun();
        std::cout << "experimental_segments=" << segment << " records=" << s.submitted << '\n';
        return 0;
    }
}

int main(int argc, char **argv)
{
    try
    {
        std::map<std::string, std::string> args;
        bool overwrite = false, grid = false;
        for (int i = 1; i < argc; ++i)
        {
            std::string key = argv[i];
            if (key == "--overwrite")
            {
                if (overwrite)
                    throw std::runtime_error("duplicate overwrite");
                overwrite = true;
                continue;
            }
            if (key == "--experiment-grid")
            {
                if (grid)
                    throw std::runtime_error("duplicate experiment mode");
                grid = true;
                continue;
            }
            if (key != "--video" && key != "--config" && key != "--output" &&
                key != "--noise-csv" && key != "--run-dir")
                throw std::runtime_error("unknown option");
            if (i + 1 == argc || !args.emplace(key, argv[++i]).second)
                throw std::runtime_error("missing/duplicate option");
        }
        if (!args.count("--config") || (!args.count("--run-dir") && !args.count("--output")))
            throw std::runtime_error("--config and --run-dir required");
        if (grid ? (!args.count("--noise-csv") || args.count("--video"))
                 : (!args.count("--video") || args.count("--noise-csv")))
            throw std::runtime_error("invalid video/experiment mode");
        auto app = mark::loadConfig(args["--config"]);
        app.offline.directory =
            args.count("--run-dir") ? args["--run-dir"] : args["--output"] + ".run";
        if (args.count("--output") && std::filesystem::exists(args["--output"]))
            throw std::runtime_error(
                "compatibility output exists; use a new path; old evidence never overwritten");
        app.offline.mode = "debug";
        if (app.diagnostics.level == "summary")
            app.diagnostics.level = "frame";
        int result = grid ? experiment(app, args["--config"], args["--noise-csv"])
                          : mark::runOffline(app, args["--video"], args["--config"],
                                             mark::ExecutionScope::Temporal);
        if (args.count("--output"))
        {
            std::ofstream note(args["--output"]);
            note << "Block5 compatibility pointer: "
                 << (std::filesystem::absolute(app.offline.directory) / "frames.jsonl").string()
                 << '\n';
            if (!note)
                throw std::runtime_error("compatibility pointer write failure");
            std::cerr << "--output已迁移到新的.run目录；该路径只保存说明文件。\n";
        }
        return result;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
