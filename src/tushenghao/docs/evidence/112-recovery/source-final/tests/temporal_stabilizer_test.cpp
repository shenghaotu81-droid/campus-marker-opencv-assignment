// 所有断言主动执行；各测试保留命名与失败原因，NDEBUG不屏蔽。
#include "temporal_fixture.hpp"
#include <limits>
using namespace mark;
using namespace temporal_fixture;

namespace
{
    void formula()
    {
        double tau = computeTimeConstantSeconds(14, .7);
        near(tau, -.014 / std::log(.3), 1e-12);
        near(computeCurrentWeight(.007, tau), 1 - std::sqrt(.3), 1e-12);
        near(computeCurrentWeight(.014, tau), .7, 1e-12);
        near(computeCurrentWeight(.028, tau), .91, 1e-12);
    }

    void parameters()
    {
        for (double v : {0., -1., std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity()})
        {
            bool rejected = false;
            try
            {
                computeTimeConstantSeconds(v, .7);
            }
            catch (const std::exception &)
            {
                rejected = true;
            }
            check(rejected, "bad reference accepted");
        }
        for (double v : {0., 1., std::numeric_limits<double>::quiet_NaN()})
        {
            bool rejected = false;
            try
            {
                computeTimeConstantSeconds(14, v);
            }
            catch (const std::exception &)
            {
                rejected = true;
            }
            check(rejected, "bad alpha accepted");
        }
        for (int missing = 0; missing < 2; ++missing)
        {
            auto c = config();
            if (missing)
                c.max_smoothing_deviation_px.reset();
            else
                c.correspondence_uncertainty_px.reset();
            bool rejected = false;
            try
            {
                TemporalStabilizer t(c);
            }
            catch (const std::exception &)
            {
                rejected = true;
            }
            check(rejected, "missing budget silently disabled");
        }
    }

    void continuous()
    {
        TemporalStabilizer t(config());
        auto raw = square();
        auto r = t.update({raw}, stamp(0, 0), {1440, 1080});
        check(same(tracked(r), raw), "first frame changed");
        raw = square(1);
        r = t.update({raw}, stamp(1, 14000), {1440, 1080});
        near(tracked(r).corners[0].x, 100.7);
        check(raw.corners[0].x == 101, "raw changed");
        check(r.diagnostics.used_smoothing, "filter unused");
        check(tracked(r).bbox == boundingBoxFromCorners(tracked(r).corners), "bbox stale");
    }

    void delta()
    {
        for (auto pair : {std::pair<int64_t, double>{7000, 1 - std::sqrt(.3)}, {28000, .91}})
        {
            TemporalStabilizer t(config());
            t.update({square()}, stamp(0, 0), {1440, 1080});
            near(tracked(t.update({square(1)}, stamp(1, pair.first), {1440, 1080})).corners[0].x,
                 100 + pair.second);
        }
    }

    void zero()
    {
        TemporalStabilizer t(config());
        t.update({square()}, stamp(0, 0), {1440, 1080});
        auto r = t.update({square(1)}, stamp(1, 0), {1440, 1080});
        check(same(tracked(r), square(1)) && !r.diagnostics.alpha &&
                  r.diagnostics.reset_or_fallback_reason == "ZERO_DT",
              "zero dt froze history");
        near(tracked(t.update({square(2)}, stamp(2, 14000), {1440, 1080})).corners[0].x, 101.7);
    }

    void empty()
    {
        TemporalStabilizer t(config());
        t.update({square()}, stamp(0, 0), {1440, 1080});
        auto r = t.update({}, stamp(1, 14000), {1440, 1080});
        check(r.status == Status::NOT_DETECTED && r.tracks.empty(), "historical track on empty");
    }

    void recovery()
    {
        TemporalStabilizer t(config());
        t.update({square()}, stamp(0, 0), {1440, 1080});
        t.update({}, stamp(1, 14000), {1440, 1080});
        auto r = t.update({square(2)}, stamp(2, 28000), {1440, 1080});
        check(same(tracked(r), square(2)) && r.diagnostics.association.matched_history,
              "recovery mixed points");
        near(tracked(t.update({square(3)}, stamp(3, 42000), {1440, 1080})).corners[0].x, 102.7);
    }

    void unknown()
    {
        TemporalStabilizer t(config());
        t.update({square()}, stamp(0, 0), {1440, 1080});
        auto r = t.update({square(1, 0, false)}, stamp(1, 14000), {1440, 1080});
        check(!tracked(r).attributes.orientation && r.diagnostics.used_smoothing,
              "unknown filled or not smoothed");
    }

    // golden始终在物理slot计算，跨44/46排序分界连续三帧以上，查内部缓存slot是否错位。
    void rotationKnown()
    {
        auto c = config();
        c.max_smoothing_deviation_px = 100;
        TemporalStabilizer t(c);
        std::array<cv::Point2d, 4> prev{};
        for (int n = 0; n < 5; ++n)
        {
            auto d = square(0, 42 + n * 2, true);
            auto r = t.update({d}, stamp(n, n * 14000), {1440, 1080});
            const auto &o = tracked(r);
            std::array<cv::Point2d, 4> now{};
            for (int p = 0; p < 4; ++p)
            {
                cv::Point2d z = d.corners[(*d.attributes.orientation)[p]];
                now[p] = n ? .7 * z + .3 * prev[p] : z;
                check(cv::norm(now[p] - cv::Point2d(o.corners[(*o.attributes.orientation)[p]])) <
                          1e-5,
                      "cross-boundary mixed physical corner");
            }
            prev = now;
        }
    }

    void rotationUnknown()
    {
        auto a = square(0, 44, false), b = square(0, 46, false);
        auto mapping = resolveCornerCorrespondence(b, a, config());
        check(mapping.valid, "44/46 not unique");
        auto ak = square(0, 44), bk = square(0, 46);
        for (int p = 0; p < 4; ++p)
            check(mapping.current_to_previous[(*bk.attributes.orientation)[p]] ==
                      size_t((*ak.attributes.orientation)[p]),
                  "wrong cyclic mapping");
        TemporalStabilizer t(config());
        t.update({a}, stamp(0, 0), {1440, 1080});
        auto r = t.update({b}, stamp(1, 14000), {1440, 1080});
        check(r.diagnostics.used_smoothing && !tracked(r).attributes.orientation,
              "unknown mapping failure");
        std::array<cv::Point2d, 4> golden{};
        for (size_t i = 0; i < 4; ++i)
            golden[i] = cv::Point2f(.7 * cv::Point2d(b.corners[i]) +
                                    .3 * cv::Point2d(a.corners[mapping.current_to_previous[i]]));
        check(same(tracked(r), fromPhysical(golden, false)), "unknown filter differs from golden");
    }

    void ambiguous()
    {
        auto a = square(0, 0, false), b = square(0, 45, false);
        check(!resolveCornerCorrespondence(b, a, config()).valid, "45deg ambiguity accepted");
        TemporalStabilizer t(config());
        t.update({a}, stamp(0, 0), {1440, 1080});
        auto r = t.update({b}, stamp(1, 14000), {1440, 1080});
        check(same(tracked(r), b) &&
                  r.diagnostics.reset_or_fallback_reason == "CORRESPONDENCE_AMBIGUOUS" &&
                  !r.diagnostics.alpha,
              "ambiguous points filtered");
        auto next = square(0, 46, false);
        r = t.update({next}, stamp(2, 28000), {1440, 1080});
        check(r.diagnostics.used_smoothing, "ambiguous fallback did not rebuild");
    }

    void associationFailure()
    {
        TemporalStabilizer t(config());
        t.update({square()}, stamp(0, 0), {1440, 1080});
        auto r = t.update({square(100)}, stamp(1, 14000), {1440, 1080});
        check(same(tracked(r), square(100)) && !r.diagnostics.association.matched_history,
              "valid new target deleted");
    }

    void multiple()
    {
        auto small = square(0, 0, true, 80), big = square(300, 0, true, 110);
        TemporalStabilizer t(config());
        auto r = t.update({small, big}, stamp(0, 0), {1440, 1080});
        check(r.tracks[0].detection_index == 1, "largest polygon not selected");
        t.reset(ResetReason::External);
        r = t.update({square(), square(400)}, stamp(0, 0), {1440, 1080});
        check(r.tracks[0].detection_index == 0, "equal area order unstable");
        r = t.update({square(400), square(1)}, stamp(1, 14000), {1440, 1080});
        check(r.tracks[0].detection_index == 1 && r.diagnostics.association.matched_history,
              "unique match not selected");
        r = t.update({square(2), square(3)}, stamp(2, 28000), {1440, 1080});
        check(r.diagnostics.association.ambiguous && same(tracked(r), square(2)),
              "two matches guessed nearest");
        r = t.update({square(400), big}, stamp(3, 42000), {1440, 1080});
        check(!r.diagnostics.association.matched_history && r.tracks[0].detection_index == 1,
              "zero matches not largest");
    }

    void boundaries()
    {
        for (int64_t gap : {50000, 50001})
        {
            TemporalStabilizer t(config());
            t.update({square()}, stamp(0, 0), {1440, 1080});
            auto r = t.update({square(1)}, stamp(1, gap), {1440, 1080});
            check(r.diagnostics.association.matched_history == (gap == 50000),
                  "history strict boundary wrong");
        }
        // 使用整数矩形精确落在面积比.5/2和参考对角线中心距离门限。
        for (double ratio : {.5, 2.})
        {
            auto p = physical();
            for (auto &x : p)
                x.y = 150 + (x.y - 150) * ratio;
            TemporalStabilizer t(config());
            t.update({square()}, stamp(0, 0), {1440, 1080});
            auto r = t.update({fromPhysical(p)}, stamp(1, 14000), {1440, 1080});
            check(r.diagnostics.association.matched_history, "area equality excluded");
        }
        auto p = physical();
        for (auto &x : p)
            x += cv::Point2d(50, 50);
        TemporalStabilizer t(config());
        t.update({square()}, stamp(0, 0), {1440, 1080});
        check(t.update({fromPhysical(p)}, stamp(1, 14000), {1440, 1080})
                  .diagnostics.association.matched_history,
              "center equality excluded");
    }

    void invalidGeometry()
    {
        auto q = square();
        std::array<cv::Point2d, 4> z;
        for (size_t i = 0; i < 4; ++i)
            z[i] = q.corners[i];
        for (int v = 0; v < 5; ++v)
        {
            auto p = z;
            if (v == 0)
                p[1] = {150, 150};
            if (v == 1)
                std::swap(p[1], p[2]);
            if (v == 2)
                p[0].x = -1;
            if (v == 3)
                p[0] = p[1];
            if (v == 4)
                p[0].x = std::numeric_limits<double>::quiet_NaN();
            std::string reason;
            check(!validateStableCorners(p, q, {1440, 1080}, 100, reason) && !reason.empty(),
                  "bad stable geometry accepted");
        }
        auto c = config();
        c.max_smoothing_deviation_px = 2;
        TemporalStabilizer t(c);
        t.update({q}, stamp(0, 0), {1440, 1080});
        auto r = t.update({square(10)}, stamp(1, 14000), {1440, 1080});
        check(same(tracked(r), square(10)) &&
                  r.diagnostics.reset_or_fallback_reason == "SMOOTHING_DEVIATION",
              "deviation clipped or accepted");
        near(tracked(t.update({square(11)}, stamp(2, 28000), {1440, 1080})).corners[0].x, 110.7);
        // 防御所有当前候选：错起点、方向重复、bbox错误、集合中一个坏项均INVALID_INPUT。
        for (int v = 0; v < 4; ++v)
        {
            auto d = q;
            if (v == 0)
                std::rotate(d.corners.begin(), d.corners.begin() + 1, d.corners.end());
            if (v == 1)
                d.attributes.orientation = std::array<int, 4>{0, 0, 2, 3};
            if (v == 2)
                d.bbox.width++;
            if (v == 3)
                d.corners[0].x = -1;
            check(t.update({q, d}, stamp(3 + v, 42000 + v), {1440, 1080}).status ==
                      Status::INVALID_INPUT,
                  "bad candidate silently dropped");
        }
    }

    void invalidSequence()
    {
        for (auto bad : {stamp(5, 100), stamp(4, 101), stamp(6, 99), stamp(6, -1),
                         FrameStamp{6, 101, static_cast<TimestampSource>(42)}})
        {
            TemporalStabilizer t(config());
            t.update({square()}, stamp(5, 100), {1440, 1080});
            auto r = t.update({square(1)}, bad, {1440, 1080});
            check(r.status == Status::INVALID_INPUT && r.tracks.empty(), "invalid stamp accepted");
            check(same(tracked(t.update({square(2)}, stamp(0, 0), {1440, 1080})), square(2)),
                  "invalid did not clear all histories");
        }
    }

    void resetSize()
    {
        TemporalStabilizer t(config());
        t.update({square()}, stamp(0, 0), {1440, 1080});
        t.reset(ResetReason::InputChanged);
        t.reset(ResetReason::InputChanged);
        check(same(tracked(t.update({square(1)}, stamp(0, 0), {1440, 1080})), square(1)),
              "loop reset leaked");
        check(same(tracked(t.update({square(2)}, stamp(1, 14000), {1500, 1100})), square(2)),
              "resize leaked");
    }

    void isolation()
    {
        TemporalStabilizer a(config()), b(config()), aa(config()), bb(config());
        for (int i = 0; i < 5; ++i)
        {
            if (i == 2)
            {
                a.reset(ResetReason::External);
                aa.reset(ResetReason::External);
            }
            auto ar = a.update({square(i)}, stamp(i, i * 14000), {1440, 1080}),
                 br = b.update({square(200 + 2 * i)}, stamp(i, i * 14000), {1440, 1080});
            auto ax = aa.update({square(i)}, stamp(i, i * 14000), {1440, 1080}),
                 bx = bb.update({square(200 + 2 * i)}, stamp(i, i * 14000), {1440, 1080});
            check(same(tracked(ar), tracked(ax)) && same(tracked(br), tracked(bx)) &&
                      ar.diagnostics.reset_or_fallback_reason ==
                          ax.diagnostics.reset_or_fallback_reason &&
                      br.diagnostics.alpha == bx.diagnostics.alpha,
                  "instance leaked");
        }
    }

    void disabled()
    {
        auto c = config();
        c.stabilization_enabled = false;
        c.correspondence_uncertainty_px.reset();
        c.max_smoothing_deviation_px.reset();
        TemporalStabilizer t(c);
        for (int n = 0; n < 3; ++n)
        {
            auto r = t.update({square(n)}, stamp(n, n * 14000), {1440, 1080});
            check(same(tracked(r), square(n)) && !r.diagnostics.alpha, "disabled filtered");
        }
        check(t.update({}, stamp(3, 42000), {1440, 1080}).tracks.empty(), "disabled held track");
    }
}

int main()
{
    return run({{"T01", formula},
                {"T02", parameters},
                {"T03", continuous},
                {"T04", delta},
                {"T05", zero},
                {"T06", empty},
                {"T07", recovery},
                {"T08", unknown},
                {"T09", rotationKnown},
                {"T10", rotationUnknown},
                {"T10b", ambiguous},
                {"T11", associationFailure},
                {"T12", multiple},
                {"T13", boundaries},
                {"T14", invalidGeometry},
                {"T15", invalidSequence},
                {"T16", resetSize},
                {"T17", isolation},
                {"T18", disabled}});
}
