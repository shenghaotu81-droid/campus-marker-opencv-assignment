#include "offline_runner.hpp"
#include "debug_renderer.hpp"
#include "display_session.hpp"
#include "input_completion.hpp"
#include "pipeline/detector_diagnostics_access.hpp"
#include "pipeline/stabilize_stage.hpp"
#include "run_console.hpp"
#include "video_exporter.hpp"
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>
#include <sstream>
#include <thread>

namespace mark
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        // App只使用标准库文件FNV指纹并明确算法名/大小；归档工具另补SHA256，不能冒称密码学摘要。
        std::string fingerprint_file(const std::filesystem::path &path)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                throw std::runtime_error("cannot fingerprint input: " + path.string());
            uint64_t hash = 14695981039346656037ull, size = 0;
            char buffer[65536];
            while (in)
            {
                in.read(buffer, sizeof buffer);
                auto n = in.gcount();
                size += uint64_t(n);
                for (std::streamsize i = 0; i < n; ++i)
                {
                    hash ^= static_cast<unsigned char>(buffer[i]);
                    hash *= 1099511628211ull;
                }
            }
            if (!in.eof())
                throw std::runtime_error("fingerprint read failed");
            std::ostringstream out;
            out << "FNV1a64:" << std::hex << std::setw(16) << std::setfill('0') << hash
                << ":size=" << std::dec << size;
            return out.str();
        }

        // 读取实际CPU名称；环境不可读取时明确UNKNOWN，不能推测硬件。
        std::string cpu()
        {
            std::ifstream in("/proc/cpuinfo");
            std::string line;
            while (std::getline(in, line))
                if (line.rfind("model name", 0) == 0)
                    return line.substr(line.find(':') + 1);
            return "UNKNOWN: cpuinfo unavailable";
        }

        // App阶段有独立边界；public total替换内部近似值，其他阶段不允许重复计时。
        void external_timing(FrameRecord &r, Stage stage, Clock::duration elapsed, bool enabled)
        {
            auto &slot = r.timings.at(size_t(stage));
            if (slot.status == TimingStatus::MEASURED && stage != Stage::ProcessTotal)
                throw std::logic_error("EXTERNAL_STAGE_DUPLICATE");
            slot = {stage, enabled ? TimingStatus::MEASURED : TimingStatus::DISABLED,
                    enabled ? std::optional<std::chrono::nanoseconds>(
                                  std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed))
                            : std::nullopt};
            if (stage == Stage::ProcessTotal)
                r.process_total_boundary = "public_call";
        }

        // 原 runner 将启动、检测和输出塞在同一函数；内部会话按职责拆分，保留顺序和计时边界。
        class OfflineRunSession
        {
          public:
            OfflineRunSession(AppConfig app, std::string video, std::string config_path,
                              ExecutionScope scope, std::set<uint64_t> subset, std::string purpose)
                : app_(std::move(app)), video_(std::move(video)),
                  config_path_(std::move(config_path)), scope_(scope), subset_(std::move(subset)),
                  purpose_(std::move(purpose)), recorder_(app_.diagnostics), pending_(subset_),
                  baseline_(app_.offline.mode == "baseline"),
                  timing_(app_.detector_config.debug.timing_enabled)
            {
            }

            int run()
            {
                validate_run();
                prepare_metadata();
                recorder_.beginRun(meta_, app_.offline.directory);
                start_input();
                wall_start_ = Clock::now();
                consume_input();
                return finish_run();
            }

          private:
            AppConfig app_;
            std::string video_, config_path_;
            ExecutionScope scope_;
            std::set<uint64_t> subset_;
            std::string purpose_;
            RunMetadata meta_;
            DiagnosticsRecorder recorder_;
            cv::VideoCapture capture_;
            double fps_{};
            std::optional<uint64_t> expected_;
            std::unique_ptr<Detector> detector_;
            std::optional<MarkerGeometry> model_;
            std::unique_ptr<TemporalStabilizer> temporal_;
            std::unique_ptr<DisplayHistory> display_;
            DisplaySession display_session_;
            VideoExporter video_exporter_;
            std::chrono::nanoseconds video_run_export_{};
            ResultFingerprint fingerprint_;
            uint64_t id_{};
            std::set<uint64_t> pending_;
            Clock::time_point wall_start_;
            cv::Mat image_;
            std::chrono::nanoseconds eof_read_{};
            bool incomplete_{false}, baseline_, timing_;

            // 原模式检查混在启动中；在任何输入或运行目录创建之前统一拒绝冲突。
            void validate_run()
            {
                // 原loader保留相对配置定位规则；runner规范已解析路径，避免effective_config重载时再拼run目录。
                app_.detector_config.marker_geometry_path_ =
                    std::filesystem::absolute(app_.detector_config.marker_geometry_path_)
                        .lexically_normal()
                        .string();
                validateConfig(app_);

                if (app_.offline.directory.empty())
                    throw std::runtime_error("run directory required");
                if (baseline_ &&
                    (!timing_ || app_.diagnostics.level != "summary" ||
                     app_.detector_config.output.show_window ||
                     app_.detector_config.output.show_held_state ||
                     app_.detector_config.debug.draw_candidates ||
                     app_.render.draw_corner_evidence || app_.offline.export_evidence ||
                     app_.offline.export_video || app_.offline.playback_fps != 0))
                    throw std::runtime_error("baseline configuration conflict");
                if (app_.offline.export_evidence &&
                    (baseline_ || app_.diagnostics.level != "evidence"))
                    throw std::runtime_error("evidence export requires debug/evidence");
            }

            // 原启动长函数难核来源；本 helper 只记录身份，不改来源和指纹。
            void prepare_metadata()
            {
                meta_.run_id = std::filesystem::path(app_.offline.directory).filename().string();
                if (meta_.run_id.empty())
                    throw std::runtime_error("invalid run directory name");
                meta_.mode = app_.offline.mode;
                meta_.run_purpose = purpose_;
                meta_.source_path = std::filesystem::absolute(video_).lexically_normal().string();
                meta_.config_path =
                    std::filesystem::absolute(config_path_).lexically_normal().string();
                meta_.model_path =
                    std::filesystem::absolute(app_.detector_config.marker_geometry_path_)
                        .lexically_normal()
                        .string();
                meta_.commit_label = MARK_COMMIT;
                meta_.code_fingerprint = MARK_CODE_HASH;
                meta_.input_fingerprint = fingerprint_file(meta_.source_path);
                meta_.environment = {
                    {"CPU", cpu()},
                    {"OS", "Linux"},
                    {"compiler", __VERSION__},
                    {"OpenCV", CV_VERSION},
                    {"build_type", MARK_BUILD_TYPE},
                    {"OpenCV_threads", std::to_string(cv::getNumThreads())},
                    {"CWD", std::filesystem::current_path().string()},
                    {"model_fingerprint", fingerprint_file(meta_.model_path)},
                    {"execution_scope", scopeName(scope_)},
                    {"execution_subset", subset_.empty() ? "all" : "explicit frame list"},
                    {"timestamp_recipe",
                     "llround(frame_id * 1e6 / video_fps); algorithm source Unknown"}};
                if (!subset_.empty())
                {
                    std::ostringstream ids;
                    for (auto id_ : subset_)
                    {
                        if (ids.tellp() > 0)
                            ids << ',';
                        ids << id_;
                    }
                    meta_.environment["execution_subset_ids"] = ids.str();
                }
            }

            // 原输入与算法实例启动混在循环；提取原顺序并保留 startup 计时。
            void start_input()
            {
                writeEffectiveConfig(app_, recorder_.directory() / "effective_config.yaml");
                const auto startup = Clock::now();
                capture_.open(meta_.source_path);
                if (!capture_.isOpened())
                    throw std::runtime_error("cannot open video");
                fps_ = capture_.get(cv::CAP_PROP_FPS);
                if (!std::isfinite(fps_) || fps_ <= 0)
                    throw std::runtime_error("invalid video fps");
                // 旧 EOF 不核数量；保留容器原值，显式期待数优先而不改写 metadata。
                const auto metadata_expected =
                    metadata_frame_count(capture_.get(cv::CAP_PROP_FRAME_COUNT));
                expected_ = app_.offline.expected_frame_count
                                ? std::optional<uint64_t>(app_.offline.expected_frame_count)
                                : metadata_expected;
                meta_.environment["final_fixes_contract"] = "1";
                meta_.environment["expected_frames"] =
                    expected_ ? std::to_string(*expected_) : "unknown";
                meta_.environment["expected_frames_source"] = app_.offline.expected_frame_count
                                                                  ? "explicit"
                                                              : metadata_expected ? "metadata"
                                                                                  : "unknown";
                meta_.environment["video_fps"] = std::to_string(fps_);
                meta_.environment["frame_count_metadata"] =
                    std::to_string(capture_.get(cv::CAP_PROP_FRAME_COUNT));
                if (scope_ == ExecutionScope::Full)
                    detector_ = std::make_unique<Detector>(app_.detector_config);
                else
                {
                    model_ = loadMarkerGeometry(meta_.model_path);
                    if (scope_ == ExecutionScope::Temporal)
                    {
                        temporal_ =
                            std::make_unique<TemporalStabilizer>(app_.detector_config.temporal);
                        display_ = std::make_unique<DisplayHistory>(
                            DisplayHistoryConfig{app_.detector_config.temporal.display_hold_enabled,
                                                 app_.detector_config.temporal.max_hold_frames});
                    }
                }
                meta_.environment["startup_us"] = std::to_string(
                    std::chrono::duration<double, std::micro>(Clock::now() - startup).count());
                writeRunManifest(meta_, (recorder_.directory() / "manifest.yaml").string());
                meta_.environment["video_export_requested"] =
                    app_.offline.export_video ? "true" : "false";
                const bool requested = app_.detector_config.output.show_window;
                display_session_.start(
                    requested, display_probe_path(),
                    std::chrono::milliseconds(app_.offline.display_probe_timeout_ms));
                meta_.environment["display_requested"] = requested ? "true" : "false";
                record_display_state();
                if (requested && !display_session_.active())
                    std::cout << "显示已降级："
                              << reason_label(display_session_.unavailable_reason()) << "（"
                              << display_session_.unavailable_reason() << "）；继续无窗口处理\n";
            }

            // 原读帧数与处理编排混写；保留每次成功读取计数及子集筛选。
            void consume_input()
            {
                for (;;)
                {
                    auto read_start = Clock::now();
                    bool got = capture_.read(image_);
                    auto read_elapsed = Clock::now() - read_start;
                    if (!got)
                    {
                        eof_read_ =
                            std::chrono::duration_cast<std::chrono::nanoseconds>(read_elapsed);
                        break;
                    }
                    if (!subset_.empty() && !subset_.count(id_))
                    {
                        ++id_;
                        continue;
                    }
                    if (app_.offline.export_video && !video_exporter_.opened())
                    {
                        auto start = Clock::now();
                        video_exporter_.open(recorder_.directory(), image_.size(), fps_,
                                             app_.offline.video_fourcc);
                        auto elapsed = Clock::now() - start;
                        video_run_export_ += elapsed;
                        recorder_.summary().run_export += elapsed;
                    }
                    process_frame(read_elapsed);
                    pending_.erase(id_);
                    ++id_;
                    if (incomplete_)
                        break;
                }
            }

            // 原逐帧公共调用和 audit 混在长循环；同次调用、异常记录及公开计时边界保持不变。
            void process_frame(Clock::duration read_elapsed)
            {
                long double timestamp = static_cast<long double>(id_) * 1e6 / fps_;
                if (!std::isfinite(timestamp) ||
                    timestamp > std::numeric_limits<int64_t>::max() - .5L)
                    throw std::runtime_error("video timestamp overflow");
                FrameInput input{image_, id_, std::llround(double(id_) * 1e6 / fps_),
                                 TimestampSource::Unknown};
                DiagnosticsRequest request;
                request.run_id = meta_.run_id;
                request.timing_enabled = timing_;
                request.scope = scope_;
                request.timestamp_recipe = "video_fps";
                request.level = app_.diagnostics.level == "evidence" ? DiagnosticsLevel::Evidence
                                : app_.diagnostics.level == "frame"  ? DiagnosticsLevel::Frame
                                                                     : DiagnosticsLevel::Summary;
                request.selected =
                    request.level != DiagnosticsLevel::Summary &&
                    id_ >= app_.diagnostics.detail_first &&
                    (!app_.diagnostics.detail_last || id_ <= *app_.diagnostics.detail_last) &&
                    (id_ - app_.diagnostics.detail_first) % app_.diagnostics.detail_interval == 0;
                FrameRecord record;
                FrameResult result{};
                if (detector_)
                {
                    auto prepare_start = Clock::now();
                    DetectorDiagnosticsAccess::prepare(*detector_, request);
                    recorder_.summary().bookkeeping += Clock::now() - prepare_start;
                    auto start = Clock::now();
                    try
                    {
                        result = detector_->process(input);
                    }
                    catch (...)
                    {
                        record = DetectorDiagnosticsAccess::take(*detector_);
                        record.run_failed = true;
                        recorder_.submit(record);
                        throw;
                    }
                    auto elapsed = Clock::now() - start;
                    auto take_start = Clock::now();
                    record = DetectorDiagnosticsAccess::take(*detector_);
                    external_timing(record, Stage::ProcessTotal, elapsed, timing_);
                    recorder_.summary().bookkeeping += Clock::now() - take_start;
                }
                else
                {
                    FrameDiagnosticsContext context;
                    context.begin(input, request);
                    auto decoded =
                        runDecodePipeline(input, app_.detector_config, *model_, &context);
                    result.frame_id = id_;
                    result.timestamp_us = input.timestamp_us;
                    result.status = decoded.status;
                    if (temporal_)
                        result = finalizeDecodedFrame(
                            decoded, {id_, input.timestamp_us, input.time_source}, image_.size(),
                            *temporal_, *display_, &context);
                    else
                        result.detections = decoded.detections;
                    if (scope_ != ExecutionScope::Geometry)
                        context.output(result);
                    context.finish();
                    record = std::move(context.record);
                }
                update_observations(record, result, read_elapsed);
                cv::Mat overlay = render_and_wait(record, result);
                export_frame(record, overlay);
            }

            // 原观测簿记嵌在检测分支后；提取且继续位于 process 外。
            void update_observations(FrameRecord &record, const FrameResult &result,
                                     Clock::duration read_elapsed)
            {
                if (!meta_.environment.count("first_submitted_frame_id"))
                    meta_.environment["first_submitted_frame_id"] = std::to_string(id_);
                auto bookkeeping_start = Clock::now();
                external_timing(record, Stage::Capture, read_elapsed, timing_);
                if (scope_ != ExecutionScope::Geometry)
                    fingerprint_.add(result);
                recorder_.summary().bookkeeping += Clock::now() - bookkeeping_start;
            }

            // 原 GUI/等待和算法混排；提取后仍只占 Visualize/Wait 槽。
            cv::Mat render_and_wait(FrameRecord &record, const FrameResult &result)
            {
                cv::Mat overlay;
                if (baseline_)
                    return overlay;
                auto start = Clock::now();
                auto render = app_.render;
                render.draw_candidates = app_.detector_config.debug.draw_candidates;
                render.show_held_state = app_.detector_config.output.show_held_state;
                auto events = validateRenderEvidence(record);
                record.events.insert(record.events.end(), events.begin(), events.end());
                overlay = renderDebugFrame(image_, result, record, render);
                const auto rendered = Clock::now() - start;
                const bool was_active = display_session_.active();
                if (was_active)
                    incomplete_ = display_session_.show_and_poll(overlay);
                // 显示降级后会话保留最后一次测量；只在本帧实际进入显示时计入，避免重复归账。
                external_timing(record, Stage::Visualize,
                                rendered + (was_active ? display_session_.visualize_elapsed()
                                                       : Clock::duration{}),
                                timing_);
                if (was_active || app_.offline.playback_fps > 0)
                {
                    auto wait_start = Clock::now();
                    if (app_.offline.playback_fps > 0)
                        std::this_thread::sleep_until(
                            wait_start +
                            std::chrono::duration_cast<Clock::duration>(
                                std::chrono::duration<double>(1 / app_.offline.playback_fps)));
                    external_timing(
                        record, Stage::Wait,
                        Clock::now() - wait_start +
                            (was_active ? display_session_.wait_elapsed() : Clock::duration{}),
                        timing_);
                }
                if (was_active && !display_session_.active())
                    std::cout << "显示已降级："
                              << reason_label(display_session_.unavailable_reason()) << '\n';
                record_display_state();
                return overlay;
            }

            // 原 manifest 没有请求与实际显示之分；仅报告本会话真实状态，不将无屏写成已显示。
            void record_display_state()
            {
                meta_.environment["display_active"] = display_session_.active() ? "true" : "false";
                meta_.environment["display_reason"] = display_session_.unavailable_reason();
            }

            // 原 export 写盘与统计混在循环；提取后仍先写记录再补完成值。
            void export_frame(const FrameRecord &record, const cv::Mat &overlay)
            {
                // 关闭阶段计时不读取或存储 Export 时长；侧表/结束报告仍属于运行级成本。
                const auto export_start = timing_ ? Clock::now() : Clock::time_point{};
                if (record.selected && app_.offline.export_evidence)
                {
                    auto dir = recorder_.directory() / "evidence";
                    std::filesystem::create_directories(dir);
                    if (!cv::imwrite((dir / (std::to_string(id_) + "-original.png")).string(),
                                     image_) ||
                        !cv::imwrite((dir / (std::to_string(id_) + "-overlay.png")).string(),
                                     overlay))
                        throw std::runtime_error("evidence image write failure");
                }
                if (app_.offline.export_video)
                    video_exporter_.write(overlay);
                recorder_.submit(record);
                if (record.selected || app_.offline.export_video)
                    recorder_.complete_export(
                        id_,
                        {Stage::Export, timing_ ? TimingStatus::MEASURED : TimingStatus::DISABLED,
                         timing_ ? std::optional<std::chrono::nanoseconds>(
                                       std::chrono::duration_cast<std::chrono::nanoseconds>(
                                           Clock::now() - export_start))
                                 : std::nullopt},
                        record.selected);
            }

            // 原结束逻辑难确认失败生命周期；串行保留完整性→manifest→summary→补充报告顺序。
            int finish_run()
            {
                // 缺失子集也保留已处理记录，以 incomplete_ 结束而不是丢掉诊断。
                const auto completion = evaluate_input_completion(id_, expected_, incomplete_,
                                                                  !subset_.empty(), pending_);
                incomplete_ = completion.incomplete;
                meta_.environment["coverage_verified"] =
                    completion.coverage_verified ? "true" : "false";
                meta_.environment["completion_reason"] = completion.reason;
                // 编码器关闭/读回发生在 recorder Running，失败会留下 FAILED 而非先发布成功 summary。
                if (app_.offline.export_video && video_exporter_.written_frames())
                {
                    auto start = Clock::now();
                    video_exporter_.finish(!incomplete_);
                    const auto elapsed = Clock::now() - start;
                    video_run_export_ += elapsed;
                    recorder_.summary().run_export += elapsed;
                    meta_.environment["video_requested_fourcc"] = app_.offline.video_fourcc;
                    meta_.environment["video_backend"] = video_exporter_.backend();
                    meta_.environment["video_readback_frames"] =
                        std::to_string(video_exporter_.written_frames());
                    meta_.environment["video_readback_fps"] =
                        std::to_string(video_exporter_.readback_fps());
                }
                if (incomplete_)
                {
                    std::ofstream marker(recorder_.directory() / "INCOMPLETE.json");
                    marker << "{\"reason\":" << quoteJson(completion.reason)
                           << ",\"frames_read\":" << id_ << ",\"expected_frames\":"
                           << (expected_ ? std::to_string(*expected_) : "null") << "}\n";
                    marker.flush();
                    if (!marker)
                        throw std::runtime_error("incomplete marker write failure");
                }
                auto &summary = recorder_.summary();
                summary.incomplete = incomplete_;
                summary.wall = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                                                    wall_start_);
                summary.result_fingerprint = fingerprint_.hex();
                summary.fingerprint_frames = fingerprint_.frames();
                meta_.environment["eof_read_us"] = std::to_string(double(eof_read_.count()) / 1000);
                meta_.environment["frames_read"] = std::to_string(id_);
                meta_.environment["coverage"] = subset_.empty() ? "full_input" : "explicit_subset";
                writeRunManifest(meta_, (recorder_.directory() / "manifest.yaml").string());
                auto final = recorder_.finishRun();
                std::ofstream export_report(recorder_.directory() / "run_export.yaml");
                export_report
                    << "run_export_us: " << std::setprecision(17)
                    << double(final.run_export.count()) / 1000
                    << "\nboundary: \"side-table writes plus finishRun summary write; this supplement "
                       "write excluded\"\n"
                    << "video_open_close_readback_rename_us: "
                    << double(video_run_export_.count()) / 1000 << '\n';
                export_report.flush();
                if (!export_report)
                    throw std::runtime_error("run export supplement write failure");
                write_run_console(std::cout, meta_, final, id_, timing_, scope_);
                return incomplete_ ? 1 : 0;
            }
        };
    } // namespace

    // 公共入口原签名冻结；内部会话拆分不改变执行范围、检测输入或结果。
    int runOffline(AppConfig app, const std::string &video, const std::string &config_path,
                   ExecutionScope scope, const std::set<uint64_t> &subset,
                   const std::string &purpose)
    {
        return OfflineRunSession(std::move(app), video, config_path, scope, subset, purpose).run();
    }
} // namespace mark
