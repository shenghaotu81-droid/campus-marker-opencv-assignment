#include "observability_fixture.hpp"
#include <thread>
#include "pipeline/diagnostics_recorder.hpp"
using namespace mark;
using namespace observability_fixture;
using namespace std::chrono;

namespace
{
    void statistics()
    {
        std::vector<nanoseconds> v;
        for (int i = 1; i <= 20; ++i)
            v.push_back(milliseconds(i));
        auto s = summarizeDurations(v);
        check(s.n == 20 && s.mean_ns == 10500000 && s.median_ns == 10500000 &&
                  s.p95_ns == 19000000 && s.p99_ns == 20000000 && s.max_ns == 20000000,
              "M01 statistics");
    }

    void empty()
    {
        auto e = summarizeDurations({});
        check(e.n == 0 && !e.mean_ns && !e.median_ns && !e.max_ns, "M02 empty");
        auto s = summarizeDurations({nanoseconds(7)});
        check(s.mean_ns == 7 && s.median_ns == 7 && s.p95_ns == 7 && s.p99_ns == 7 && s.max_ns == 7,
              "M02 singleton");
    }

    void statuses()
    {
        FrameTiming t(true);
        t.addMeasured(Stage::Preprocess, nanoseconds(7));
        t.markStatus(Stage::Detect, TimingStatus::DISABLED);
        t.markStatus(Stage::Decode, TimingStatus::NOT_IMPLEMENTED);
        auto s = t.snapshot();
        check(s[1].elapsed == nanoseconds(7) && s[2].status == TimingStatus::DISABLED &&
                  !s[2].elapsed && s[3].status == TimingStatus::NOT_IMPLEMENTED &&
                  s[4].status == TimingStatus::SKIPPED && s[0].status == TimingStatus::NOT_EXECUTED,
              "M03 explicit states");
    }

    void early(FrameTiming &t)
    {
        ScopedStageTimer timer(t, Stage::Decode);
        return;
    }

    void raii()
    {
        for (int n = 0; n < 3; ++n)
        {
            FrameTiming t(true);
            try
            {
                if (n == 1)
                    early(t);
                else
                {
                    ScopedStageTimer timer(t, Stage::Decode);
                    if (n == 2)
                        throw std::runtime_error("fixture");
                }
            }
            catch (const std::runtime_error &)
            {
            }
            check(t.snapshot()[3].status == TimingStatus::MEASURED && t.snapshot()[3].elapsed &&
                      t.snapshot()[3].elapsed->count() >= 0,
                  "M04 RAII");
            if (n == 2)
            {
                auto path = std::filesystem::temp_directory_path() /
                            ("block5-exception-" +
                             std::to_string(steady_clock::now().time_since_epoch().count()));
                DiagnosticsRecorder recorder({});
                RunMetadata meta;
                meta.run_id = "failure";
                recorder.beginRun(meta, path);
                FrameRecord record;
                record.run_id = meta.run_id;
                record.run_failed = true;
                record.timings = t.snapshot();
                rejects(
                    [&]
                    {
                        recorder.submit(record);
                    });
                check(std::filesystem::exists(path / "FAILED.json") &&
                          !std::filesystem::exists(path / "summary.yaml"),
                      "M04 exception was reported as success");
                std::filesystem::remove_all(path);
            }
        }
    }

    void duplicate()
    {
        FrameTiming t(true);
        {
            ScopedStageTimer timer(t, Stage::Detect);
            rejects(
                [&]
                {
                    ScopedStageTimer another(t, Stage::Detect);
                });
        }
        rejects(
            [&]
            {
                t.addMeasured(Stage::Detect, nanoseconds(1));
            });
        FrameTiming off;
        {
            ScopedStageTimer timer(off, Stage::Decode);
        }
        check(off.snapshot()[3].status == TimingStatus::DISABLED && !off.snapshot()[3].elapsed,
              "M05 disabled");
        rejects(
            [&]
            {
                off.addMeasured(Stage::Preprocess, nanoseconds(1));
            });
    }

    void wait()
    {
        FrameTiming t(true);
        {
            ScopedStageTimer process(t, Stage::ProcessTotal);
        }
        auto saved = t.snapshot()[5].elapsed;
        {
            ScopedStageTimer outside(t, Stage::Wait);
            std::this_thread::sleep_for(milliseconds(1));
        }
        check(t.snapshot()[5].elapsed == saved && t.snapshot()[7].status == TimingStatus::MEASURED,
              "M06 separate wait");
    }

    void replace()
    {
        FrameTiming t(true);
        t.addMeasured(Stage::ProcessTotal, nanoseconds(7));
        t.replaceProcessTotal(nanoseconds(11));
        check(t.snapshot()[5].elapsed == nanoseconds(11), "M07 replacement");
    }

    void first()
    {
        auto s = summarizeDurations({milliseconds(100), milliseconds(1), milliseconds(1)});
        check(s.n == 3 && s.max_ns == 100000000 && s.median_ns == 1000000,
              "M08 first/slow omitted");
    }
}

int main()
{
    return temporal_fixture::run({{"M01", statistics},
                                  {"M02", empty},
                                  {"M03", statuses},
                                  {"M04", raii},
                                  {"M05", duplicate},
                                  {"M06", wait},
                                  {"M07", replace},
                                  {"M08", first}});
}
