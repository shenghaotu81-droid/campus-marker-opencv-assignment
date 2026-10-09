// r=2、偏离=20只是主动断言fixture，不能写生产YAML或视作预算审批。
#pragma once
#include "pipeline/temporal_stabilizer.hpp"
#include "corners/screen_order.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace temporal_fixture
{
    inline void check(bool ok, const std::string &message)
    {
        if (!ok)
            throw std::runtime_error(message);
    }

    inline void near(double a, double b, double eps = 1e-5)
    {
        check(std::abs(a - b) <= eps,
              "numeric mismatch: " + std::to_string(a) + " vs " + std::to_string(b));
    }

    inline mark::TemporalConfig config()
    {
        mark::TemporalConfig c;
        c.correspondence_uncertainty_px = 2;
        c.max_smoothing_deviation_px = 20;
        return c;
    }

    inline mark::FrameStamp stamp(uint64_t id, int64_t time)
    {
        return {id, time, mark::TimestampSource::Unknown};
    }

    // 以实际共享排序生成合法当前Detection，输出方向始终是物理→屏幕映射。
    inline mark::Detection fromPhysical(const std::array<cv::Point2d, 4> &p, bool known = true)
    {
        std::array<cv::Point2d, 4> rounded = p;
        for (auto &x : rounded)
            x = cv::Point2f(x);
        std::string reason;
        auto order = mark::orderScreenCycle(rounded, {}, reason);
        check(bool(order), reason);
        mark::Detection d{};
        for (size_t i = 0; i < 4; ++i)
            d.corners[i] = order->screen_points[i];
        d.bbox = mark::boundingBoxFromCorners(d.corners);
        if (known)
            d.attributes.orientation = order->input_to_screen;
        return d;
    }

    inline std::array<cv::Point2d, 4> physical(double dx = 0, double angle = 0, double edge = 100,
                                               cv::Point2d center = {150, 150})
    {
        std::array<cv::Point2d, 4> p{{{-edge / 2, -edge / 2},
                                      {edge / 2, -edge / 2},
                                      {edge / 2, edge / 2},
                                      {-edge / 2, edge / 2}}};
        double a = angle * CV_PI / 180, cs = std::cos(a), sn = std::sin(a);
        for (auto &q : p)
            q = {center.x + dx + cs * q.x - sn * q.y, center.y + sn * q.x + cs * q.y};
        return p;
    }

    inline mark::Detection square(double dx = 0, double angle = 0, bool known = true,
                                  double edge = 100)
    {
        return fromPhysical(physical(dx, angle, edge), known);
    }

    inline const mark::Detection &tracked(const mark::TemporalResult &r)
    {
        check(r.status == mark::Status::DETECTED && r.tracks.size() == 1, "missing track");
        return r.tracks[0].result;
    }

    inline bool same(const mark::Detection &a, const mark::Detection &b)
    {
        return a.corners == b.corners && a.bbox == b.bbox &&
               a.attributes.orientation == b.attributes.orientation &&
               a.attributes.marker_code.has_value() == b.attributes.marker_code.has_value() &&
               a.confidence == b.confidence && a.category == b.category &&
               a.quality_flags == b.quality_flags;
    }

    inline int run(const std::vector<std::pair<std::string, void (*)()>> &cases)
    {
        int failures = 0;
        for (const auto &c : cases)
            try
            {
                c.second();
                std::cout << "PASS " << c.first << '\n';
            }
            catch (const std::exception &e)
            {
                ++failures;
                std::cerr << "FAIL " << c.first << ": " << e.what() << '\n';
            }
        return failures ? 1 : 0;
    }
}
