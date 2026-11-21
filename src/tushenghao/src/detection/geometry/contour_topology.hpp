#pragma once
#include <algorithm>
#include <opencv2/imgproc.hpp>

namespace mark::detector
{
    // 点到线段的距离，投影超出端点时使用端点距离。
    inline double segment_distance(cv::Point2d p, cv::Point2d a, cv::Point2d b)
    {
        auto v = b - a;
        double n = v.dot(v);
        if (!(n > 0))
            return cv::norm(p - a);

        return cv::norm(p - (a + v * std::clamp((p - a).dot(v) / n, 0.0, 1.0)));
    }

    // 将相邻边叉积乘有向面积。同号为凸，异号为凹，零值退化。
    inline double normalized_turn(const std::vector<cv::Point2d> &p, size_t i)
    {
        auto a = p[i] - p[(i + p.size() - 1) % p.size()], b = p[(i + 1) % p.size()] - p[i];
        double area = 0;

        for (size_t j = 0; j < p.size(); ++j)
        {
            auto q = p[j], r = p[(j + 1) % p.size()];
            area += q.x * r.y - q.y * r.x;
        }

        return (a.x * b.y - a.y * b.x) * area;
    }

    // 直接接收六点轮廓。七点轮廓只允许删掉一个点，且每条合并边必须覆盖对应原弧。
    inline std::vector<std::vector<cv::Point2d>> corner_topology_polygons(const std::vector<cv::Point> &contour_,
                             const std::vector<cv::Point> &simple, double epsilon)
    {
        std::vector<std::vector<cv::Point2d>> result;

        if (simple.size() == 6)
        {
            result.emplace_back(simple.begin(), simple.end());
            return result;
        }

        if (simple.size() != 7)
            return result;

        // 找到简化顶点在原轮廓中的位置。
        std::vector<size_t> indices;

        for (auto p : simple)
        {
            auto it = std::find(contour_.begin(), contour_.end(), p);
            indices.push_back(size_t(it - contour_.begin()));
        }

        for (size_t omit = 0; omit < 7; ++omit)
        {
            std::vector<cv::Point2d> polygon;
            std::vector<size_t> selected;
            for (size_t i = 0; i < 7; ++i)
                if (i != omit)
                {
                    polygon.push_back(simple[i]);
                    selected.push_back(indices[i]);
                }

            bool bounded = true;
            for (size_t i = 0; i < 6 && bounded; ++i)
            {
                size_t begin = selected[i], end = selected[(i + 1) % 6];
                for (size_t k = begin;; k = (k + 1) % contour_.size())
                {
                    if (segment_distance(contour_[k], polygon[i], polygon[(i + 1) % 6]) > epsilon)
                    {
                        bounded = false;
                        break;
                    }
                    if (k == end)
                        break;
                }
            }

            if (bounded)
                result.push_back(std::move(polygon));
        }
        return result;
    }
}
