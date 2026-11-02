#include "case_runner.hpp"
#include "observability_fixture.hpp"
#include "temporal_fixture.hpp"
#include "recording/diagnostics_recorder.hpp"
#include "pipeline/stabilize_stage.hpp"
#include "common/frame_record_reader.hpp"
#include "corners/corner_evidence_validation.hpp"
#include "corners/corner_resolver.hpp"
#include "corners/detection_validator.hpp"
#include "corners/periodic_line_fit.hpp"
#include "corners/screen_order.hpp"
#include <fstream>
using namespace mark;
using namespace observability_fixture;
using namespace mark::validation;

namespace
{
    std::filesystem::path dir()
    {
        return std::filesystem::temp_directory_path() /
               ("block5-record-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    }

    FrameRecord record(uint64_t id = 0, bool selected = true)
    {
        FrameDiagnosticsContext context;
        auto r = request();
        r.run_id = "test";
        r.selected = selected;
        context.begin(frame(id, id * 14000), r);
        context.finish();
        return context.record;
    }

    void escapes()
    {
        auto r = record();
        r.events.push_back({Stage::Decode,
                            ReasonCode::OtherRecordedReason,
                            "中文\"\\\n\t\x01",
                            r.frame_id,
                            {},
                            {},
                            1});
        auto j = readJson(serializeFrameRecord(r));
        check(j.at("events").array[0].at("detail").text == r.events[0].detail,
              "R01 escape roundtrip");
        check(readJson("\"\\uD83D\\uDE00\"").text == "😀", "unicode surrogate");
        for (auto s : {"{\"a\":1,\"a\":2}", "null true", "\"\\uD800\"", "[1,]", "{", "01", "1e999",
                       "\"\\x\""})
            rejects(
                [&]
                {
                    readJson(s);
                });
    }

    void finite()
    {
        auto r = record(UINT64_MAX);
        r.source_timestamp_us = INT64_MIN;
        r.enqueue_to_result_us = std::numeric_limits<double>::quiet_NaN();
        r.counts.components = 0;
        auto j = readJson(serializeFrameRecord(r));
        check(j.at("frame_id").u64() == UINT64_MAX &&
                  j.at("source_timestamp_us").i64() == INT64_MIN,
              "R02 64bit precision");
        check(j.at("counts").at("components").u64() == 0 && j.at("counts").at("tracks").null(),
              "R02 null vs zero");
        check(j.at("realtime").at("enqueue_to_result_us").null() &&
                  j.at("events").array.back().at("reason").text == "NonFiniteField",
              "R02 nonfinite event");
    }

    void lifecycle()
    {
        auto path = dir();
        DiagnosticsRecorder recorder({});
        rejects(
            [&]
            {
                recorder.submit(record());
            });
        RunMetadata m;
        m.run_id = "test";
        recorder.beginRun(m, path);
        rejects(
            [&]
            {
                recorder.beginRun(m, dir());
            });
        DiagnosticsRecorder other({});
        rejects(
            [&]
            {
                other.beginRun(m, path);
            });
        recorder.submit(record(0, false));
        recorder.finishRun();
        rejects(
            [&]
            {
                recorder.finishRun();
            });
        check(!std::filesystem::exists(path / "FAILED.json"), "R03 existing directory damaged");
        auto blocked = dir();
        {
            std::ofstream out(blocked);
            out << "preserve";
        }
        DiagnosticsRecorder fail({});
        rejects(
            [&]
            {
                fail.beginRun(m, blocked / "run");
            });
        std::ifstream in(blocked);
        std::string content;
        in >> content;
        check(content == "preserve", "R03 old file changed");
        std::filesystem::remove_all(path);
        std::filesystem::remove(blocked);
    }

    void sampling()
    {
        for (auto level : {"summary", "frame", "evidence"})
        {
            DiagnosticsConfig config;
            config.level = level;
            auto path = dir();
            DiagnosticsRecorder recorder(config);
            RunMetadata m;
            m.run_id = "test";
            recorder.beginRun(m, path);
            for (uint64_t id = 0; id < 100; ++id)
                recorder.submit(record(id, id % 10 == 0 && config.level != "summary"));
            auto s = recorder.finishRun();
            check(s.submitted == 100 && s.selected == (config.level == "summary" ? 0 : 10),
                  "R04 sampling diluted denominator");
            check(std::filesystem::exists(path / "frames.jsonl") == bool(config.level != "summary"),
                  "R04 summary detailed file");
            std::filesystem::remove_all(path);
        }
    }

    void scopes()
    {
        for (auto scope : {ExecutionScope::Full, ExecutionScope::Geometry, ExecutionScope::Decode,
                           ExecutionScope::Temporal})
        {
            FrameDiagnosticsContext c;
            auto r = request();
            r.scope = scope;
            c.begin(frame(), r);
            c.finish();
            auto j = readJson(serializeFrameRecord(c.record));
            check(j.at("timings").array.size() == 9, "R05 slots");
            if (scope != ExecutionScope::Full)
                check(c.record.timings[5].status == TimingStatus::NOT_EXECUTED,
                      "R05 fake process total");
            check(!c.record.timings[1].elapsed &&
                      c.record.timings[1].status == TimingStatus::SKIPPED,
                  "R05 skipped time");
        }
    }

    void exports()
    {
        DiagnosticsConfig config;
        config.level = "frame";
        auto path = dir();
        DiagnosticsRecorder r(config);
        RunMetadata m;
        m.run_id = "test";
        r.beginRun(m, path);
        r.submit(record());
        r.addExportTiming(0, std::chrono::nanoseconds(7));
        rejects(
            [&]
            {
                r.addExportTiming(0, std::chrono::nanoseconds(7));
            });
        auto s = r.finishRun();
        check(s.durations[8].size() == 1 && s.durations[8][0].count() == 7, "R06 export sample");
        std::ifstream in(path / "export_timings.jsonl");
        std::string line;
        std::getline(in, line);
        check(readJson(line).at("elapsed_us").number() == .007, "R06 precision");
        std::filesystem::remove_all(path);
    }

    // 旧断言只测开启计时；关闭、未采样视频和首帧补记必须分别覆盖。
    void export_contract()
    {
        for (bool timing : {false, true})
            for (bool selected : {false, true})
            {
                DiagnosticsConfig config;
                config.level = "frame";
                auto path = dir();
                DiagnosticsRecorder r(config);
                RunMetadata m;
                m.run_id = "test";
                r.beginRun(m, path);
                r.submit(record(7, selected));
                StageTiming completed{
                    Stage::Export, timing ? TimingStatus::MEASURED : TimingStatus::DISABLED,
                    timing ? std::optional<std::chrono::nanoseconds>(std::chrono::nanoseconds(70))
                           : std::nullopt};
                rejects(
                    [&]
                    {
                        r.complete_export(6, completed, selected);
                    });
                rejects(
                    [&]
                    {
                        r.complete_export(7, {Stage::Wait, completed.status, completed.elapsed},
                                          selected);
                    });
                rejects(
                    [&]
                    {
                        r.complete_export(7, {Stage::Export, TimingStatus::MEASURED, std::nullopt},
                                          selected);
                    });
                rejects(
                    [&]
                    {
                        r.complete_export(
                            7, {Stage::Export, TimingStatus::DISABLED, std::chrono::nanoseconds(0)},
                            selected);
                    });
                rejects(
                    [&]
                    {
                        r.complete_export(
                            7,
                            {Stage::Export, TimingStatus::MEASURED, std::chrono::nanoseconds(-1)},
                            selected);
                    });
                rejects(
                    [&]
                    {
                        r.complete_export(7, completed, !selected);
                    });
                r.complete_export(7, completed, selected);
                rejects(
                    [&]
                    {
                        r.complete_export(7, completed, selected);
                    });
                r.submit(record(8, false));
                auto summary = r.finishRun();
                check(summary.durations[8].size() == (timing ? 1 : 0),
                      "disabled export created measurement");
                check((*summary.first_frame)[8].status == completed.status &&
                          (*summary.first_frame)[8].elapsed == completed.elapsed,
                      "first export snapshot stale");
                check(summary.timing_states[8][size_t(TimingStatus::NOT_EXECUTED)] == 1,
                      "no export status lost");
                std::ifstream in(path / "export_timings.jsonl");
                std::string line;
                std::getline(in, line);
                auto j = readJson(line);
                check(j.at("selected_record").boolean == selected &&
                          j.at("elapsed_us").null() == !timing,
                      "side export contract wrong");
                std::filesystem::remove_all(path);
            }
    }

    void resets()
    {
        ResetSnapshot saturated;
        saturated.counts[0] = UINT64_MAX;
        saturated.add(ResetReason::External);
        check(saturated.saturated && saturated.counts[0] == UINT64_MAX &&
                  saturated.last == ResetReason::External,
              "R07 saturation/last reason");
        TemporalStabilizer t(app().detector_config.temporal);
        DisplayHistory display({false, 5});
        t.reset(ResetReason::External);
        t.reset(ResetReason::InputChanged);
        FrameDiagnosticsContext context;
        auto req = request();
        req.scope = ExecutionScope::Temporal;
        context.begin(frame(), req);
        DecodeStageResult decoded;
        decoded.status = Status::INVALID_INPUT;
        finalizeDecodedFrame(decoded, {0, 0, TimestampSource::Unknown}, {1440, 1080}, t, display,
                             &context);
        uint64_t resets = 0;
        for (const auto &e : context.record.events)
            resets += e.occurrences;
        check(resets == 3 && context.record.last_reset_reason == ReasonCode::ResetInvalidSequence &&
                  context.record.last_reset_source_frame_id == 0,
              "R07 reset count/last reason lost");
        check(context.record.events[0].source_frame_id == std::nullopt, "R07 external source");
        decoded.status = Status::DETECTED;
        decoded.detections = {temporal_fixture::square()};
        context.begin(frame(), req);
        auto result = finalizeDecodedFrame(decoded, {0, 0, TimestampSource::Unknown}, {1440, 1080},
                                           t, display, &context);
        check(result.status == Status::DETECTED, "R07 recovery");
        context.begin(frame(1, 14000), req);
        decoded.detections[0].bbox.x += 1;
        result = finalizeDecodedFrame(decoded, {1, 14000, TimestampSource::Unknown}, {1440, 1080},
                                      t, display, &context);
        context.output(result);
        context.finish();
        auto json = readJson(serializeFrameRecord(context.record));
        check(result.status == Status::INVALID_INPUT &&
                  json.at("details").at("temporal").at("reset_or_fallback_reason").text ==
                      "INVALID_RAW_BBOX",
              "R07 actual update failure erased by reset/serialization");
        decoded.detections = {temporal_fixture::square()};
        context.begin(frame(), req);
        result = finalizeDecodedFrame(decoded, {0, 0, TimestampSource::Unknown}, {1440, 1080}, t,
                                      display, &context);
        check(result.status == Status::DETECTED && !context.details->temporal.used_smoothing,
              "R07 invalid update restored old smoothing history");
    }

    void evidence()
    {
        auto cfg = app().detector_config;
        Detector d(cfg);
        DetectorDiagnosticsAccess::prepare(d, request());
        d.process(frame(0, 0, true));
        auto r = DetectorDiagnosticsAccess::take(d);
        auto j = readJson(serializeFrameRecord(r));
        for (const auto &observation : j.at("details").at("observations").array)
            check(observation.at("simplification_error").null(), "unmeasured simplification error is not null");
        const auto &evidence = j.at("details").at("decode").at("measurements").array[0].array[0];
        const auto &old = r.details->decoded.measurements[0].evidence_[0];
        check(evidence.at("evidence_id").text == old.stable_id_ &&
                  evidence.at("support_arcs").array[0].array.size() ==
                      old.original_support_arcs_[0].size() &&
                  evidence.at("measurement_error_px").number() == old.error_ &&
                  evidence.at("physical").i64() == int(old.physical_corner_),
              "R08 original evidence lost");
    }

    // A07→V03→D16：真实证据经 JSON 往返后仍独立复算，协议不能靠默认值补齐。
    void evidence_protocol()
    {
        auto config = fixture::cornerConfig();
        Detector detector(app().detector_config);
        DetectorDiagnosticsAccess::prepare(detector, request());
        detector.process(frame(0, 0, true));
        auto current = DetectorDiagnosticsAccess::take(detector);

        const auto verify = [](FrameRecord record, const CornerConfig &config)
        {
            const auto source = readJson(serializeFrameRecord(record));
            const auto &raw = source.at("details").at("decode").at("measurements").array.at(0);
            std::set<size_t> ids;
            for (const auto &e : record.details->decoded.measurements.at(0).evidence_)
                ids.insert(e.component_id_);
            auto decoded = readCornerMeasurement(raw, record.frame_id, ids);
            const auto order = orderScreenCorners(decoded.physical_corners_, config);
            check(bool(order.screen_order_), "F21 decoded corner ordering failed");
            const auto result = validateDetectionGeometry(decoded, *order.screen_order_,
                                                         {1440, 1080}, config);
            check(result.valid_, result.rejection_reason_);
            auto details = std::make_shared<StageDetails>(*record.details);
            details->decoded.measurements[0] = decoded;
            record.details = details;
            check(readJson(serializeFrameRecord(record)).at("details").at("decode")
                      .at("measurements").array[0] == raw,
                  "F21 evidence fields changed during roundtrip");

            // 五组新增字段、版本、形状及本帧身份分别篡改；不能只查 encoder 的键存在。
            for (const auto &field : {"line_model_kinds", "periodic_axes", "periodic_coefficients",
                                      "model_mean_residual_px", "model_max_residual_px",
                                      "line_mean_residual_px", "line_max_residual_px"})
            {
                auto changed = raw;
                auto &number = changed.array[0].object.at(field).array[0];
                number.text = field == std::string("line_model_kinds") ? "99" :
                              field == std::string("periodic_axes") ? "2" : "999";
                bool rejected = false;
                try
                {
                    const auto invalid = readCornerMeasurement(changed, record.frame_id, ids);
                    std::string reason;
                    rejected = !validateCornerEvidence(invalid.evidence_[0],
                                                       invalid.physical_corners_[0], 0,
                                                       config, reason);
                }
                catch (const std::exception &)
                {
                    rejected = true;
                }
                check(rejected, "F21 changed line metadata accepted");
            }
            for (const auto &field : {"evidence_schema_version", "line_model_kinds",
                                      "periodic_axes", "periodic_coefficients",
                                      "model_mean_residual_px", "model_max_residual_px"})
            {
                auto missing = raw;
                missing.array[0].object.erase(field);
                rejects([&] { readCornerMeasurement(missing, record.frame_id, ids); });
            }
            auto malformed = raw;
            malformed.array[0].object.at("periodic_axes").array.pop_back();
            rejects([&] { readCornerMeasurement(malformed, record.frame_id, ids); });
            rejects([&] { readCornerMeasurement(raw, record.frame_id + 1, ids); });
            rejects([&] { readCornerMeasurement(raw, record.frame_id, {}); });
            malformed = raw;
            malformed.array[0].object.at("evidence_schema_version").text = "99";
            rejects([&] { readCornerMeasurement(malformed, record.frame_id, ids); });
        };
        verify(current, app().detector_config.corner_);

        // 复用已有周期证据契约的非零奇偶位移场景，防止 codec 偷把系数或模型残差写成零。
        auto scene = fixture::scene(1440, 1080, 2.0);
        const auto image = scene.frame.original_image_.clone();
        scene.frame.original_image_.setTo(cv::Scalar(0, 0, 0));
        for (int y = 0; y < image.rows; ++y)
            for (int x = 0; x < image.cols; ++x)
            {
                const int target = x + (y % 2 ? 1 : -1);
                if (target >= 0 && target < image.cols)
                    scene.frame.original_image_.at<cv::Vec3b>(y, target) = image.at<cv::Vec3b>(y, x);
            }
        auto periodic_config = fixture::cornerConfig();
        periodic_config.min_line_points_ = 10;
        periodic_config.max_line_fit_error_ = 0.5;
        periodic_config.observation_budget_ = CornerObservationBudget{3, 9, 4.5, 2, 13.5, 9.5, 14};
        const auto observed = resolveObservedCorners(scene.frame, scene.hypothesis, scene.geometry,
                                                     periodic_config);
        check(bool(observed.measurement_), observed.rejection_reason_);
        bool nonzero = false;
        for (const auto &e : observed.measurement_->evidence_)
            for (double coefficient : e.periodic_coefficients_)
                nonzero |= coefficient != 0;
        check(nonzero, "F21 periodic fixture has no nonzero coefficient");
        auto periodic_record = record(scene.frame.frame_id_);
        // 手工注入已取到的测量时同步标明执行过的阶段，缺测详情按原协议仍是 null。
        periodic_record.counts.generated = 0;
        periodic_record.counts.measurements = 1;
        auto periodic_details = std::make_shared<StageDetails>();
        periodic_details->decoded.measurements.push_back(*observed.measurement_);
        periodic_record.details = periodic_details;
        verify(periodic_record, periodic_config);

        // 手工直角/连续整数支持弧：四个周期估计器都应还原同一组可复算实测线。
        for (auto model : {LineFitModel::PeriodicLeastSquares, LineFitModel::PeriodicPhaseMedian,
                           LineFitModel::PeriodicL1, LineFitModel::PeriodicZeroSlope})
        {
            auto details = std::make_shared<StageDetails>();
            CornerMeasurement measurement;
            const std::array<cv::Point2d, 4> corners{{{100, 100}, {200, 100},
                                                      {200, 200}, {100, 200}}};
            for (size_t i = 0; i < corners.size(); ++i)
            {
                auto &e = measurement.evidence_[i];
                const auto p = corners[i];
                measurement.physical_corners_[i] = e.intersection_ = p;
                e.physical_corner_ = static_cast<PhysicalCorner>(i);
                e.frame_id_ = current.frame_id;
                e.component_id_ = i;
                e.stable_id_ = "F21-" + std::to_string(i);
                e.model_vertex_id_ = i == 1 ? 1 : 0;
                e.model_edge_ids_ = i == 1 ? std::array<int, 2>{0, 1} :
                                           std::array<int, 2>{5, 0};
                e.observed_segment_ids_ = {0, 1};
                e.observed_segment_end_ids_ = {2, 3};
                e.original_observation_ = true;
                e.truncated_ = false;
                for (int offset = 10; offset >= 3; --offset)
                    e.original_support_arcs_[0].push_back(p + cv::Point2d(0, offset));
                for (int offset = 3; offset <= 10; ++offset)
                    e.original_support_arcs_[1].push_back(p + cv::Point2d(offset, 0));
                e.original_turn_arc_ = {p + cv::Point2d(0, 3), p + cv::Point2d(0, 2),
                                        p + cv::Point2d(0, 1), p, p + cv::Point2d(1, 0),
                                        p + cv::Point2d(2, 0), p + cv::Point2d(3, 0)};
                for (size_t edge = 0; edge < 2; ++edge)
                {
                    const auto fit = fitPeriodicLine(e.original_support_arcs_[edge], model);
                    check(bool(fit), "F21 periodic fixture did not fit");
                    (edge ? e.line_b_ : e.line_a_) = fit->line;
                    e.line_model_kinds_[edge] = model;
                    e.periodic_axes_[edge] = fit->axis;
                    e.periodic_coefficients_[edge] = fit->coefficient;
                    const auto &support = e.original_support_arcs_[edge];
                    (edge ? e.edge_segment_b_ : e.edge_segment_a_) =
                        std::array<cv::Point2d, 2>{support.front(), support.back()};
                }
                e.line_mean_residual_px_ = e.line_max_residual_px_ = {0, 0};
                e.model_mean_residual_px_ = e.model_max_residual_px_ = {0, 0};
                e.support_extension_px_ = {3, 3};
                e.corner_error_px_ = e.error_ = 0;
            }
            details->decoded.measurements.push_back(measurement);
            current.details = details;
            verify(current, config);
        }
    }

    void config()
    {
        auto a = app();
        a.offline.mode = "debug";
        a.offline.directory = "fixture.run";
        a.diagnostics.level = "evidence";
        a.diagnostics.detail_first = 2;
        a.diagnostics.detail_last = 8;
        a.diagnostics.detail_interval = 3;
        a.render.draw_corner_evidence = true;
        a.render.draw_raw = false;
        a.render.draw_stable = false;
        a.render.draw_timing = true;
        a.offline.playback_fps = 20;
        a.offline.export_evidence = true;
        auto p = dir();
        writeEffectiveConfig(a, p);
        auto b = loadConfig(p);
        check(b.offline.mode == a.offline.mode && b.offline.directory == a.offline.directory &&
                  b.diagnostics.detail_last == 8 && b.render.draw_corner_evidence &&
                  !b.render.draw_raw && !b.render.draw_stable && b.render.draw_timing &&
                  b.offline.playback_fps == 20 && b.offline.export_evidence &&
                  b.diagnostics.detail_first == 2 && b.diagnostics.detail_interval == 3,
              "R09 effective config lost");
        std::filesystem::remove(p);
    }
}

int main()
{
    return test_support::run({{"F05_export_contract", export_contract},
                                  {"R01", escapes},
                                  {"R02", finite},
                                  {"R03", lifecycle},
                                  {"R04", sampling},
                                  {"R05", scopes},
                                  {"R06", exports},
                                  {"R07", resets},
                                  {"R08", evidence},
                                  {"F21_evidence_protocol", evidence_protocol},
                                  {"R09", config}});
}
