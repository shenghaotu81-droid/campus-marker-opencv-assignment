// 沿原图角片轮廓提取两组连续边支持，各拟合一次直线并以交点作为精修角。
#include "corner_edge_fit.hpp"
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <limits>
namespace mark::detector
{
    std::optional<cv::Point2d> fit_observed_corner(const std::vector<cv::Point> &contour_,
                                                 cv::Point2d corner, const std::array<cv::Point2d, 2> &adjacent, std::string &reason_)
    {
        auto fail = [&](const char *s)->std::optional<cv::Point2d>
                    {
                        reason_ = s;
                        return {};
                    };
        reason_.clear();
        if (contour_.size() < 2 || !std::isfinite(corner.x) || !std::isfinite(corner.y))
            return fail("MISSING_CONTOUR");
        std::array<cv::Point2d, 2> directions{adjacent[0] - corner, adjacent[1] - corner};
        for (auto &v:directions)
        {
            double n = cv::norm(v);
            if (!std::isfinite(n) || n == 0)
                return fail("INVALID_GUIDANCE");
            v *= 1 / n;
        }
        size_t anchor = 0;
        for (size_t i = 1; i < contour_.size(); ++i)
            if (cv::norm(cv::Point2d(contour_[i]) - corner) < cv::norm(cv::Point2d(contour_[anchor]) - corner))
                anchor = i;

        const int count = int(contour_.size());
        auto point = [&](int k)
                     {
                         return cv::Point2d(contour_[(k % count + count) % count]);
                     };
        // 3像素半窗平滑CHAIN_APPROX_NONE的栅格台阶，2像素trim去除转折混合。
        // 都只定义支持提取，不是拟合后的质量拒绝预算；不按帧/设备调路线。
        constexpr int tangent_window = 3;
        constexpr double trim = 2;
        auto bounds = cv::boundingRect(contour_);
        const int probe = std::max(3, std::max(bounds.width, bounds.height) / 4);
        cv::Vec4d lines[2];
        int previous_group = -1;
        for (int side = 0; side < 2; ++side)
        {
            int sign = side ? 1 : -1;
            const auto motion = point(int(anchor) + sign * probe) - point(int(anchor));
            int group = motion.dot(directions[1]) > motion.dot(directions[0]);
            if (group == previous_group)
                return fail("NO_TWO_DISTINCT_EDGES");
            previous_group = group;
            std::vector<cv::Point2d> support;
            bool started = false;
            double walk = 0;
            auto last = point(int(anchor));
            for (int step = 1; step < count / 2; ++step)
            {
                int k = int(anchor) + sign * step;
                auto p = point(k);
                walk += cv::norm(p - last);
                last = p;
                if (walk < trim + tangent_window)
                    continue;
                const auto tangent = point(k + tangent_window) - point(k - tangent_window);
                int label = std::abs(tangent.dot(directions[1])) > std::abs(tangent.dot(directions[0]));
                if (label != group)
                {
                    if (started)
                        break;
                    continue;
                }
                started = true;
                support.push_back(p);
            }
            double tail = 0;
            while (support.size() > 1 && tail < trim)
            {
                tail += cv::norm(support.back() - support[support.size() - 2]);
                support.pop_back();
            }
            if (support.size() < 2 || support.front() == support.back())
                return fail("NO_LINE_SUPPORT");
            cv::fitLine(support, lines[side], cv::DIST_L2, 0, .01, .01);
        }
        cv::Point2d a(lines[0][2], lines[0][3]), b(lines[1][2], lines[1][3]);
        cv::Point2d v(lines[0][0], lines[0][1]), w(lines[1][0], lines[1][1]);
        double cross = v.cross(w);
        if (!std::isfinite(cross) || std::abs(cross) <= std::numeric_limits<double>::epsilon())
            return fail("PARALLEL_LINES");
        const auto intersection = a + v * ((b - a).cross(w) / cross);
        if (!std::isfinite(intersection.x) || !std::isfinite(intersection.y))
            return fail("NONFINITE_INTERSECTION");
        return intersection;
    }

}
