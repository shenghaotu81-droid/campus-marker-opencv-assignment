// A/B独立合成语义只验桥接，绝不构造MarkerCode或改有效四点。
#include "pipeline/display_history.hpp"
#include "temporal_fixture.hpp"
using namespace mark;
using namespace temporal_fixture;

namespace
{
    SemanticDisplaySample sample(const char *value, uint64_t id, int64_t time)
    {
        return {value, id, time};
    }

    // 合成文字来源/年龄/held单独输出，便于归档5/6边界实际状态，不进入生产审计。
    void logDisplay(uint64_t id, const std::optional<DisplayState> &state)
    {
        std::cout << "DISPLAY_JSONL {\"frame_id\":" << id << ",\"display\":";
        if (state)
            std::cout << "{\"value\":\"" << state->value
                      << "\",\"source_frame_id\":" << state->source_frame_id
                      << ",\"age\":" << state->age
                      << ",\"held\":" << (state->is_held ? "true" : "false") << '}';
        else
            std::cout << "null";
        std::cout << "}\n";
    }

    void hold()
    {
        DisplayHistory h({true, 5});
        auto s = h.update(sample("A", 0, 0), stamp(0, 0));
        check(s && s->value == "A" && !s->is_held && s->age == 0, "current A invalid");
        logDisplay(0, s);
        for (int i = 1; i <= 6; ++i)
        {
            s = h.update(std::nullopt, stamp(i, i * 14000));
            logDisplay(i, s);
            if (i <= 5)
                check(s && s->value == "A" && s->source_frame_id == 0 && s->age == uint64_t(i) &&
                          s->is_held,
                      "hold boundary wrong");
            else
                check(!s, "sixth held");
        }
        check(!h.update(std::nullopt, stamp(7, 98000)), "expired source resurrected");
    }

    void zeroHold()
    {
        DisplayHistory h({true, 5});
        check(!h.update(std::nullopt, stamp(0, 0)), "first null invented source");
        DisplayHistory z({true, 0});
        z.update(sample("A", 0, 0), stamp(0, 0));
        check(!z.update(std::nullopt, stamp(1, 1)), "zero hold held");
    }

    void replacement()
    {
        DisplayHistory h({true, 5});
        h.update(sample("A", 0, 0), stamp(0, 0));
        h.update(std::nullopt, stamp(1, 1));
        auto s = h.update(sample("B", 2, 2), stamp(2, 2));
        check(s && s->value == "B" && !s->is_held && s->age == 0 && s->source_frame_id == 2,
              "B not replaced");
        s = h.update(std::nullopt, stamp(3, 3));
        check(s && s->value == "B" && s->source_frame_id == 2 && s->age == 1, "old A held");
    }

    void skip()
    {
        DisplayHistory h({true, 5});
        h.update(sample("A", 0, 0), stamp(0, 0));
        check(h.update(std::nullopt, stamp(100, 1))->age == 1, "age used id delta");
        check(h.update(std::nullopt, stamp(200, 2))->age == 2, "age used id delta");
    }

    void cleanup()
    {
        DisplayHistory off({false, 5});
        check(off.update(sample("A", 0, 0), stamp(0, 0))->value == "A", "disabled not passthrough");
        check(!off.update(std::nullopt, stamp(1, 1)), "disabled cached");
        for (auto bad :
             {stamp(0, 0), stamp(2, -1), FrameStamp{2, 2, static_cast<TimestampSource>(42)}})
        {
            DisplayHistory h({true, 5});
            h.update(sample("A", 0, 0), stamp(0, 0));
            check(!h.update(std::nullopt, bad), "invalid held");
            check(!h.update(std::nullopt, stamp(0, 0)), "invalid retained source/sequence");
        }
        DisplayHistory h({true, 5});
        h.update(sample("A", 0, 0), stamp(0, 0));
        h.reset(ResetReason::InputChanged);
        check(!h.update(std::nullopt, stamp(0, 0)), "reset retained text");
        check(!h.update(sample("B", 8, 8), stamp(1, 1)), "foreign semantic source accepted");
        bool rejected = false;
        try
        {
            DisplayHistory invalid({true, -1});
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        check(rejected, "negative hold accepted");
    }
}

int main()
{
    return run(
        {{"H01", hold}, {"H02", zeroHold}, {"H03", replacement}, {"H04", skip}, {"H05", cleanup}});
}
