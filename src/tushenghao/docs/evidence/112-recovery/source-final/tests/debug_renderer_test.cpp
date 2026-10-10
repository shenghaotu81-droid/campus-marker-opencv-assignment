#include "observability_fixture.hpp"
#include "debug_renderer.hpp"
#include <opencv2/imgproc.hpp>
using namespace mark;
using namespace observability_fixture;

namespace
{
    FrameResult output()
    {
        FrameResult r{};
        r.status = Status::DETECTED;
        r.detections = {temporal_fixture::square()};
        r.tracks = {{0, r.detections[0]}};
        return r;
    }

    FrameRecord record()
    {
        FrameRecord r;
        r.frame_id = 0;
        return r;
    }

    void immutable()
    {
        auto original = frame().image, before = original.clone();
        auto r = output();
        auto image = renderDebugFrame(original, r, record(), {});
        check(image.data != original.data && cv::norm(original, before, cv::NORM_INF) == 0 &&
                  cv::norm(image, original, cv::NORM_INF) > 0,
              "V01 renderer modified input");
    }

    void empty()
    {
        auto original = frame().image;
        for (auto status : {Status::NOT_DETECTED, Status::INVALID_INPUT, Status::NOT_READY})
        {
            auto r = output();
            r.status = status;
            auto image = renderDebugFrame(original, r, record(), {});
            check(image.at<cv::Vec3b>(100, 100) == cv::Vec3b(0, 0, 0), "V02 stale effective point");
        }
    }

    void history()
    {
        auto r = output();
        r.status = Status::NOT_DETECTED;
        r.detections.clear();
        r.tracks.clear();
        r.display_state = DisplayState{0, 5, true, "A"};
        RenderConfig config;
        config.draw_raw = false;
        config.draw_stable = false;
        config.show_held_state = true;
        auto image = renderDebugFrame(frame().image, r, record(), config);
        check(cv::countNonZero(image.reshape(1)) > 0 &&
                  image.at<cv::Vec3b>(100, 100) == cv::Vec3b(0, 0, 0),
              "V03 historical geometry drawn");
    }

    void unknown()
    {
        auto r = output();
        r.detections[0].attributes.orientation.reset();
        r.tracks[0].result.attributes.orientation.reset();
        auto history = record();
        history.output = output();
        auto image = renderDebugFrame(frame().image, r, history, {});
        auto known = renderDebugFrame(frame().image, output(), history, {});
        check(cv::norm(image(cv::Rect(100, 80, 100, 20)), known(cv::Rect(100, 80, 100, 20)),
                       cv::NORM_INF) > 0,
              "V04 old P label survived unknown");
        check(!r.detections[0].attributes.orientation &&
                  !r.tracks[0].result.attributes.orientation && !image.empty(),
              "V04 history direction filled");
    }

    void mapping()
    {
        for (int width : {480, 960})
        {
            PreparedFrame p;
            p.scale_x_ = double(width) / 1440;
            p.scale_y_ = double(width * 3 / 4) / 1080;
            auto x = p.workToOriginal({50.25, 60.75});
            double goldx = (50.25 + .5) * 1440 / width - .5,
                   goldy = (60.75 + .5) * 1080 / (width * 3 / 4) - .5;
            check(x.x == goldx && x.y == goldy && x.x != 50.25 * 1440 / width,
                  "V05 pixel center mapping");
            auto d = std::make_shared<StageDetails>();
            d->prepared = p;
            d->prepared.frame_id_ = 0;
            WhiteComponent c;
            c.contour_ = {{50, 61}, {70, 61}, {70, 81}, {50, 81}};
            d->components.push_back(c);
            auto r = record();
            r.details = d;
            RenderConfig config;
            config.draw_raw = config.draw_stable = false;
            config.draw_candidates = true;
            auto image = renderDebugFrame(frame().image, output(), r, config);
            auto point = p.workToOriginal({50, 61});
            cv::Point golden(cvRound((50 + .5) * 1440 / width - .5),
                             cvRound((61 + .5) * 1080 / (width * 3 / 4) - .5));
            check(cv::Point(point) == golden && image.at<cv::Vec3b>(golden) != cv::Vec3b(0, 0, 0),
                  "V05 candidate did not use golden pixel center");
        }
    }

    void mismatch()
    {
        auto r = record();
        auto details = std::make_shared<StageDetails>();
        details->decoded.measurements.resize(1);
        details->decoded.measurements[0].evidence_[0].frame_id_ = 99;
        r.details = details;
        check(!validateRenderEvidence(r).empty(), "V06 mismatch event missing");
        RenderConfig c;
        c.draw_corner_evidence = true;
        c.draw_raw = c.draw_stable = false;
        auto image = renderDebugFrame(frame().image, output(), r, c);
        check(image.at<cv::Vec3b>(100, 100) == cv::Vec3b(0, 0, 0),
              "V06 wrong-frame evidence drawn");
    }

    void off()
    {
        RenderConfig c;
        c.draw_raw = c.draw_stable = c.draw_candidates = c.draw_corner_evidence = c.draw_timing =
            c.show_held_state = false;
        auto original = frame().image;
        auto image = renderDebugFrame(original, output(), record(), c);
        check(cv::norm(image, original, cv::NORM_INF) == 0, "V07 disabled layers present");
    }
}

int main()
{
    return temporal_fixture::run({{"V01", immutable},
                                  {"V02", empty},
                                  {"V03", history},
                                  {"V04", unknown},
                                  {"V05", mapping},
                                  {"V06", mismatch},
                                  {"V07", off}});
}
