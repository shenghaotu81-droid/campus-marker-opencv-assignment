#include "geometry/matching/match_quad.hpp"
#include "geometry/observation/contour_topology.hpp"
#include <algorithm>

namespace mark
{
    namespace
    {
// L/M均为六边一凹；外角与凹点在轮廓上相隔三顶点，不能用短笔画端点冒充外角。
        bool cornerTopology(const WhiteComponent &component, const GeometryConfig &config,
                            cv::Point corner)
        {
            for (double fraction : {1., .5, .75, 1.25, 1.5, 2.})
            {
                const double epsilon = config.approximation_epsilon_ * fraction;
                std::vector<cv::Point> simple;
                cv::approxPolyDP(component.contour_, simple, epsilon, true);
                for (const auto &p : cornerTopologyPolygons(component.contour_, simple, epsilon))
                {
                    int concave = 0;
                    bool flat = false;
                    for (size_t i = 0; i < 6; ++i)
                    {
                        const double turn = normalizedTurn(p, i);
                        concave += turn < 0;
                        flat |= turn == 0;
                    }
                    if (concave != 1 || flat)
                        continue;
                    size_t outer = 0, notch = 0;
                    for (size_t i = 0; i < 6; ++i)
                    {
                        if (normalizedTurn(p, i) < 0)
                            notch = i;
                        if (cv::norm(p[i] - cv::Point2d(corner)) <
                            cv::norm(p[outer] - cv::Point2d(corner)))
                            outer = i;
                    }
                    if (outer == (notch + 3) % 6)
                        return true;
                }
            }
            return false;
        }

        bool adjacent(const cv::Rect &a, const cv::Rect &b)
        {
            const double dx = std::max({0, a.x - b.br().x, b.x - a.br().x});
            const double dy = std::max({0, a.y - b.br().y, b.y - a.br().y});
            const double span = std::max({a.width, a.height, b.width, b.height});
            return std::hypot(dx, dy) <= span;
        }

    } // namespace

    std::vector<MatchQuad> matchQuads(const PreparedFrame &frame,
                                      const std::vector<WhiteComponent> &components,
                                      const GeometryConfig &config)
    {
        std::vector<bool> visited(components.size(), false);
        std::vector<MatchQuad> result;
        for (size_t seed = 0; seed < components.size(); ++seed)
        {
            if (visited[seed])
                continue;
            std::vector<size_t> members{seed};
            visited[seed] = true;
            // 实测局部归组；S可作连通桥，但不产生模型身份或绑定搜索。
            for (size_t k = 0; k < members.size(); ++k)
                for (size_t j = 0; j < components.size(); ++j)
                    if (!visited[j] && adjacent(components[members[k]].bounding_box_,
                                                components[j].bounding_box_))
                    {
                        visited[j] = true;
                        members.push_back(j);
                    }

            std::vector<cv::Point> pixels, hull, q;
            for (auto i : members)
                pixels.insert(pixels.end(), components[i].contour_.begin(), components[i].contour_.end());
            if (pixels.size() < 3)
                continue;
            cv::convexHull(pixels, hull);
            for (double fraction : {1., .5, .75, 1.25, 1.5, 2.})
            {
                cv::approxPolyDP(hull, q, config.approximation_epsilon_ * fraction, true);
                if (q.size() == 4)
                    break;
            }
            if (q.size() != 4 || !cv::isContourConvex(q))
                continue;
            if (cv::contourArea(q, true) < 0)
                std::reverse(q.begin(), q.end());
            std::array<size_t, 4> owner{};
            int short_count = 0;
            bool valid = true;
            for (size_t i = 0; i < 4; ++i)
            {
                double best = INFINITY;
                for (auto j : members)
                {
                    const double distance = std::abs(cv::pointPolygonTest(components[j].contour_, q[i], true));
                    if (distance < best)
                        best = distance, owner[i] = j;
                }
                for (size_t j = 0; j < i; ++j)
                    if (owner[j] == owner[i])
                        valid = false;

                const auto &component = components[owner[i]];
                if (component.touches_border_ || !cornerTopology(component, config, q[i]))
                {
                    valid = false;
                    continue;
                }
                double span = 0;
                for (size_t j : {(i + 3) % 4, (i + 1) % 4})
                {
                    const cv::Point2d d = cv::Point2d(q[j]) - cv::Point2d(q[i]);
                    double lo = INFINITY, hi = -INFINITY;
                    for (auto p : component.contour_)
                    {
                        const double t = (cv::Point2d(p) - cv::Point2d(q[i])).dot(d) / d.dot(d);
                        lo = std::min(lo, t);
                        hi = std::max(hi, t);
                    }
                    span = std::max(span, hi - lo);
                }
                // 图纸30mm与14mm外臂的中点22mm；归一化到80mm框边，这里只确认三L一M，不分配物理身份。
                if (span < 22. / 80)
                {
                    ++short_count;
                }
            }
            if (!valid || short_count != 1)
                continue;
            MatchQuad match;
            for (int i = 0; i < 4; ++i)
                match[i] = frame.workToOriginal(q[i]);
            result.push_back(match);
        }
        return result;
    }

} // namespace mark
