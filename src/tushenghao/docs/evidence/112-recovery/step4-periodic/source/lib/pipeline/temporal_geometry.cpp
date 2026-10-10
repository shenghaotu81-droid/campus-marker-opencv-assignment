#include "pipeline/temporal_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace mark
{
    // 优先可信物理映射，否则仅允许同绕向循环；比较原始测量，避免滤波滞后参与对应。
    CornerCorrespondence resolveCornerCorrespondence(const Detection &c, const Detection &p,
                                                     const TemporalConfig &config)
    {
        CornerCorrespondence r;
        // helper也防御映射格式，内部测试/工具误传不能触发数组越界。
        for (const auto *d : {&c, &p})
            if (d->attributes.orientation)
            {
                std::array<bool, 4> used{};
                for (int index : *d->attributes.orientation)
                {
                    if (index < 0 || index >= 4 || used[index])
                    {
                        r.reason = "INVALID_ORIENTATION";
                        return r;
                    }
                    used[index] = true;
                }
            }
        if (c.attributes.orientation && p.attributes.orientation)
        {
            for (size_t i = 0; i < 4; ++i)
                r.current_to_previous[(*c.attributes.orientation)[i]] =
                    (*p.attributes.orientation)[i];
            r.valid = true;
            r.reason = "PHYSICAL_CORRESPONDENCE";
            return r;
        }
        if (!config.correspondence_uncertainty_px)
        {
            r.reason = "CORRESPONDENCE_BUDGET_MISSING";
            return r;
        }
        const double d2 =
            double(p.bbox.width) * p.bbox.width + double(p.bbox.height) * p.bbox.height;
        if (!(d2 > 0) || !std::isfinite(d2))
        {
            r.reason = "REFERENCE_DEGENERATE";
            return r;
        }
        std::array<double, 4> e{}, lo{}, hi{};
        for (size_t k = 0; k < 4; ++k)
            for (size_t i = 0; i < 4; ++i)
            {
                double d =
                    cv::norm(cv::Point2d(c.corners[i]) - cv::Point2d(p.corners[(i + k) % 4]));
                double a = std::max(0.0, d - 2 * (*config.correspondence_uncertainty_px)),
                       b = d + 2 * (*config.correspondence_uncertainty_px);
                e[k] += d * d / d2;
                lo[k] += a * a / d2;
                hi[k] += b * b / d2;
            }
        r.energy = e;
        r.lower = lo;
        r.upper = hi;
        size_t unique = 0, winner = 0;
        for (size_t k = 0; k < 4; ++k)
        {
            bool separated = true;
            for (size_t j = 0; j < 4; ++j)
                if (j != k)
                {
                    double tol = 64 * std::numeric_limits<double>::epsilon() *
                                 std::max({1.0, std::abs(hi[k]), std::abs(lo[j])});
                    if (!(lo[j] - hi[k] > tol))
                        separated = false;
                }
            if (separated)
            {
                ++unique;
                winner = k;
            }
        }
        if (unique != 1)
        {
            r.reason = "CORRESPONDENCE_AMBIGUOUS";
            return r;
        }
        for (size_t i = 0; i < 4; ++i)
            r.current_to_previous[i] = (i + winner) % 4;
        r.valid = true;
        r.reason = "CYCLIC_CORRESPONDENCE";
        return r;
    }

    // 正凸环同时排除自交与重复；画内边界严格，不裁点或放宽运动预算。
    bool validateStableCorners(const std::array<cv::Point2d, 4> &z, const Detection &current,
                               cv::Size size, double max_deviation, std::string &reason)
    {
        if (size.width <= 0 || size.height <= 0 || !std::isfinite(max_deviation) ||
            max_deviation < 0)
        {
            reason = "GEOMETRY_ARGUMENT";
            return false;
        }
        for (size_t i = 0; i < 4; ++i)
        {
            if (!std::isfinite(z[i].x) || !std::isfinite(z[i].y))
            {
                reason = "NON_FINITE";
                return false;
            }
            if (z[i].x < 0 || z[i].y < 0 || z[i].x >= size.width || z[i].y >= size.height)
            {
                reason = "OUT_OF_BOUNDS";
                return false;
            }
            if (cv::norm(z[i] - cv::Point2d(current.corners[i])) > max_deviation)
            {
                reason = "SMOOTHING_DEVIATION";
                return false;
            }
        }
        for (size_t i = 0; i < 4; ++i)
        {
            auto a = z[(i + 1) % 4] - z[i], b = z[(i + 2) % 4] - z[(i + 1) % 4];
            if (!(a.cross(b) > 0))
            {
                reason = "NON_CONVEX_OR_DEGENERATE";
                return false;
            }
        }
        reason.clear();
        return true;
    }

    cv::Rect2f boundingBoxFromCorners(const std::array<cv::Point2f, 4> &p)
    {
        float x = p[0].x, y = p[0].y, x1 = x, y1 = y;
        for (auto q : p)
        {
            x = std::min(x, q.x);
            y = std::min(y, q.y);
            x1 = std::max(x1, q.x);
            y1 = std::max(y1, q.y);
        }
        return {x, y, x1 - x, y1 - y};
    }
}
