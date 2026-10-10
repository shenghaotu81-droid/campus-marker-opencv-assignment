#include "observability_fixture.hpp"
#include "pipeline/diagnostics_recorder.hpp"
#include "pipeline/stabilize_stage.hpp"
#include "common/frame_record_reader.hpp"
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
        const auto &evidence = j.at("details").at("decode").at("measurements").array[0].array[0];
        const auto &old = r.details->decoded.measurements[0].evidence_[0];
        check(evidence.at("evidence_id").text == old.stable_id_ &&
                  evidence.at("support_arcs").array[0].array.size() ==
                      old.original_support_arcs_[0].size() &&
                  evidence.at("measurement_error_px").number() == old.error_ &&
                  evidence.at("physical").i64() == int(old.physical_corner_),
              "R08 original evidence lost");
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
    return temporal_fixture::run({{"F05_export_contract", export_contract},
                                  {"R01", escapes},
                                  {"R02", finite},
                                  {"R03", lifecycle},
                                  {"R04", sampling},
                                  {"R05", scopes},
                                  {"R06", exports},
                                  {"R07", resets},
                                  {"R08", evidence},
                                  {"R09", config}});
}
