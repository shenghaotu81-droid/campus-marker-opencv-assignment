// 在相邻白片中匹配三 L 一短 M，并返回原图粗四角。
#include "geometry/match_quad.hpp"
#include "geometry/contour_topology.hpp"
#include <algorithm>

namespace mark::detector
{
    namespace
    {
        // 用同一组尺度简化白片轮廓和组轮廓。
        constexpr std::array<double, 6> approximation_scales{1.0, 0.5, 0.75, 1.25, 1.5, 2.0};

        // 判断角片是否为六边一凹，且最近外角满足 outer == (notch + 3) % 6。
        bool corner_topology(const WhiteComponent &component, const GeometryConfig &config,
                             cv::Point corner)
        {
            for (double fraction : approximation_scales)
            {
                const double epsilon = config.approximation_epsilon_ * fraction;
                std::vector<cv::Point> simple;
                cv::approxPolyDP(component.contour_, simple, epsilon, true);

                for (const auto &p : corner_topology_polygons(component.contour_, simple, epsilon))
                {
                    std::array<double, 6> turns;
                    int concave = 0;
                    bool flat = false;
                    for (size_t i = 0; i < 6; ++i)
                    {
                        // 叉积与有向面积同号对应凸点，异号对应凹点，不依赖图像 y 轴朝向。
                        const double turn = turns[i] = normalized_turn(p, i);
                        concave += (turn < 0);
                        flat |= (turn == 0);
                    }

                    if (concave != 1 || flat)
                        continue;

                    size_t outer = 0, notch = 0;
                    for (size_t i = 0; i < 6; ++i)
                    {
                        if (turns[i] < 0)
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
    }

    std::vector<MatchQuad> match_quads(const PreparedFrame &frame,
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

            for (size_t k = 0; k < members.size(); ++k)
                for (size_t j = 0; j < components.size(); ++j)

                    if (!visited[j] &&
                        adjacent(components[members[k]].bounding_box_, components[j].bounding_box_))
                    {
                        visited[j] = true;
                        members.push_back(j);
                    }

            std::vector<cv::Point> pixels, hull, q;

            for (auto i : members)
                pixels.insert(pixels.end(), components[i].contour_.begin(),
                              components[i].contour_.end());
            if (pixels.size() < 3)
                continue;

            cv::convexHull(pixels, hull);

            for (double fraction : approximation_scales)
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
                    const double distance =
                        std::abs(cv::pointPolygonTest(components[j].contour_, q[i], true));

                    if (distance < best)
                        best = distance, owner[i] = j;
                }

                for (size_t j = 0; j < i; ++j)
                    if (owner[j] == owner[i])
                        valid = false;

                const auto &component = components[owner[i]];
                if (component.touches_border_ || !corner_topology(component, config, q[i]))
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

                // 按角片跨度区分唯一的短 M。
                if (span < 22.0 / 80)
                {
                    ++short_count;
                }
            }

            if (!valid || short_count != 1)
                continue;

            MatchQuad match;
            for (int i = 0; i < 4; ++i)
                // 将工作图粗角换算到原图像素中心。
                match[i] = frame.work_to_original(q[i]);
            result.push_back(match);
        }
        return result;
    }
}
