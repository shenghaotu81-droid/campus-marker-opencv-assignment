// 真实decode与公共process逐字段对照；fixture预算显式独立，不写生产配置。
#include "pipeline/stabilize_stage.hpp"
#include "config/config.hpp"
#include "mark/detector.hpp"
#include "block3_fixture.hpp"
#include "temporal_fixture.hpp"
#include "corners/corner_types.hpp"
#include "corners/detection_publication.hpp"
#include <filesystem>
using namespace mark;
using namespace temporal_fixture;

namespace
{
    DetectorConfig full()
    {
        auto c = loadConfig(std::filesystem::path(__FILE__).parent_path().parent_path() /
                            "config/detector.yaml")
                     .detector_config;
        c.temporal = config();
        return c;
    }

    FrameInput image(uint64_t id, int64_t time, bool target = false)
    {
        FrameInput f{};
        f.frame_id = id;
        f.timestamp_us = time;
        f.time_source = TimestampSource::Unknown;
        f.image = target ? fixture::scene().frame.original_image_
                         : cv::Mat(1080, 1440, CV_8UC3, cv::Scalar(0, 0, 0));
        return f;
    }

    void publicStatus()
    {
        for (int v = 0; v < 4; ++v)
        {
            auto c = full();
            if (v == 0)
                c.assignment_completion_.reset();
            if (v == 1)
                c.corner_.observation_budget_.reset();
            if (v >= 2)
                c.temporal.correspondence_uncertainty_px.reset();
            if (v == 3)
                c.temporal.stabilization_enabled = false;
            Detector d(c);
            auto r = d.process(image(0, 0, true));
            check(r.status == Status::NOT_READY && r.detections.empty() && r.tracks.empty() &&
                      !r.display_state,
                  "missing budget gate bypassed");
        }
        Detector d(full());
        auto r = d.process(image(0, 0));
        check(r.status == Status::NOT_DETECTED && r.tracks.empty() && r.frame_id == 0 &&
                  r.timestamp_us == 0,
              "ready black frame wrong status");
        r = d.process(image(1, 14000, true));
        check(r.status == Status::DETECTED && r.detections.size() == 1 && r.tracks.size() == 1 &&
                  !r.display_state,
              "real MARK failed");
    }

    void layerIsolation()
    {
        TemporalStabilizer t(config());
        DisplayHistory h({true, 5});
        h.update(SemanticDisplaySample{"A", 0, 0}, stamp(0, 0));
        DecodeStageResult d;
        d.status = Status::NOT_DETECTED;
        auto r = finalizeDecodedFrame(d, stamp(1, 14000), {1440, 1080}, t, h);
        check(r.detections.empty() && r.tracks.empty() && r.display_state &&
                  r.display_state->value == "A",
              "display affected effective outputs");
        d.status = Status::DETECTED;
        d.detections = {square()};
        finalizeDecodedFrame(d, stamp(2, 28000), {1440, 1080}, t, h);
        d.detections = {square(1, 0, false)};
        r = finalizeDecodedFrame(d, stamp(3, 42000), {1440, 1080}, t, h);
        check(!r.detections[0].attributes.orientation && !r.tracks[0].result.attributes.orientation,
              "history filled orientation");
        d.status = Status::NOT_DETECTED;
        bool rejected = false;
        try
        {
            finalizeDecodedFrame(d, stamp(4, 56000), {1440, 1080}, t, h);
        }
        catch (const std::logic_error &)
        {
            rejected = true;
        }
        check(rejected, "status payload mismatch ignored");
        // 主动构造内部显示源后验证非法帧同时清三状态，合法低id能重新开始。
        d.status = Status::DETECTED;
        d.detections = {square()};
        finalizeDecodedFrame(d, stamp(10, 100000), {1440, 1080}, t, h);
        h.update(SemanticDisplaySample{"B", 11, 114000}, stamp(11, 114000));
        r = finalizeDecodedFrame(d, stamp(10, 100000), {1440, 1080}, t, h);
        check(r.status == Status::INVALID_INPUT && !r.display_state && r.tracks.empty() &&
                  r.detections.empty(),
              "invalid shared stage retained payload");
        r = finalizeDecodedFrame(d, stamp(0, 0), {1440, 1080}, t, h);
        check(r.status == Status::DETECTED && !r.display_state &&
                  same(r.tracks[0].result, d.detections[0]),
              "three states not cleared");
        d.status = Status::NOT_READY;
        r = finalizeDecodedFrame(d, stamp(0, 0), {1440, 1080}, t, h);
        check(r.status == Status::NOT_READY && r.detections.empty() && r.tracks.empty() &&
                  !r.display_state,
              "decode notready payload leaked");
    }

    void rawEquality()
    {
        auto c = full();
        Detector d(c);
        auto model = loadMarkerGeometry(c.marker_geometry_path_);
        for (int n = 0; n < 3; ++n)
        {
            auto f = image(n, n * 14000, true);
            auto raw = runDecodePipeline(f, c, model);
            auto r = d.process(f);
            check(r.status == raw.status && r.detections.size() == raw.detections.size(),
                  "raw status/size changed");
            for (size_t i = 0; i < raw.detections.size(); ++i)
                check(same(raw.detections[i], r.detections[i]), "raw field changed");
            check(!r.tracks[0].result.confidence && !r.tracks[0].result.attributes.marker_code,
                  "semantic/confidence fabricated");
            check(r.tracks[0].result.bbox == boundingBoxFromCorners(r.tracks[0].result.corners),
                  "stable bbox mismatch");
        }
    }

    void originalSize()
    {
        auto c = full();
        c.preprocess.work_width = 480;
        c.preprocess.work_height = 360;
        Detector d(c);
        auto r = d.process(image(0, 0, true));
        check(r.status == Status::DETECTED && r.tracks[0].result.corners[0].x > 480,
              "working size used for output bounds");
        TemporalStabilizer t(config());
        DisplayHistory h({false, 5});
        DecodeStageResult decoded;
        decoded.status = Status::DETECTED;
        decoded.detections = {square(0, 0, true, 80), square(300, 0, true, 120)};
        r = finalizeDecodedFrame(decoded, stamp(0, 0), {1440, 1080}, t, h);
        check(r.detections.size() == 2 && r.tracks.size() == 1 && r.tracks[0].detection_index == 1,
              "nonzero current reference lost");
    }

    void publicCleanup()
    {
        for (int v = 0; v < 5; ++v)
        {
            Detector d(full());
            check(d.process(image(5, 100, true)).status == Status::DETECTED,
                  "no history established");
            auto bad = image(6, 101, true);
            if (v == 0)
                bad.image.release();
            if (v == 1)
                bad.frame_id = 5;
            if (v == 2)
                bad.frame_id = 4;
            if (v == 3)
                bad.timestamp_us = 99;
            if (v == 4)
                bad.time_source = static_cast<TimestampSource>(77);
            auto r = d.process(bad);
            check(r.status == Status::INVALID_INPUT && r.tracks.empty() && r.detections.empty() &&
                      !r.display_state,
                  "invalid payload leaked");
            r = d.process(image(0, 0, true));
            check(r.status == Status::DETECTED && same(r.detections[0], r.tracks[0].result),
                  "illegal did not clear lifecycle");
        }
        Detector a(full()), b(full());
        a.process(image(0, 0, true));
        b.process(image(0, 0, true));
        a.reset(ResetReason::InputChanged);
        check(a.process(image(0, 0, true)).status == Status::DETECTED, "loop reset failed");
        check(b.process(image(1, 14000, true)).status == Status::DETECTED, "other instance reset");
    }

    void sortEquality()
    {
        // 保留旧入口并逐15度和边界样例比较共享核心返回的点/映射/tie/失败。
        for (int angle = 0; angle < 360; angle += 15)
        {
            auto p = physical(0, angle);
            std::string why;
            auto cycle = orderScreenCycle(p, {}, why);
            auto old = orderScreenCorners(p, {});
            check(cycle && old.screen_order_ &&
                      cycle->screen_points == old.screen_order_->screen_points_ &&
                      cycle->input_to_screen == old.screen_order_->physical_to_screen_ &&
                      cycle->tie == old.screen_order_->screen_order_tie_,
                  "shared sorting differs");
        }
        for (int v = 0; v < 3; ++v)
        {
            auto p = physical(0, 45);
            if (v == 1)
                p[0] = p[1];
            if (v == 2)
                p[0].x = std::numeric_limits<double>::quiet_NaN();
            std::string why;
            auto a = orderScreenCycle(p, {}, why);
            auto b = orderScreenCorners(p, {});
            check(bool(a) == bool(b.screen_order_) && why == b.rejection_reason_,
                  "tie/failure sorting differs");
        }
    }

    // 原fixture预先舍入绕开了生产发布边界；以helper真实产物走共享finalize并连续跨界。
    void publishedRawAccepted()
    {
        for (bool known : {false, true})
        {
            TemporalStabilizer t(config());
            DisplayHistory h({false, 5});
            uint64_t id = 0;
            for (double angle : {44.999999, 45.000001, 45., 135., 225., 315.})
            {
                CornerMeasurement m{};
                m.physical_corners_ = physical(0, angle);
                std::string why;
                auto raw = publishFloatDetection(m, known, {300, 300}, {}, why);
                check(bool(raw), why);
                DecodeStageResult d;
                d.status = Status::DETECTED;
                d.detections = {*raw};
                auto r = finalizeDecodedFrame(d, stamp(id, id * 14000), {300, 300}, t, h);
                ++id;
                check(r.status == Status::DETECTED && r.tracks.size() == 1 &&
                          same(r.detections[0], *raw),
                      "Route B raw rejected or changed");
                check(r.tracks[0].result.attributes.orientation.has_value() == known,
                      "Route B direction availability changed");
            }
        }
    }
}

int main()
{
    return run({{"I01", publicStatus},
                {"I02", layerIsolation},
                {"I03", rawEquality},
                {"I04", originalSize},
                {"I05", publicCleanup},
                {"I06", sortEquality},
                {"P01_P04_raw_acceptance", publishedRawAccepted}});
}
