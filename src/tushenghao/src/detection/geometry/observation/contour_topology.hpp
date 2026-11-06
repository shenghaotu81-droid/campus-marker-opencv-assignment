#pragma once
#include "vision/math/observed_geometry.hpp"
#include <opencv2/imgproc.hpp>
namespace mark
{
    // 面积绕向归一化凹凸，反向遍历轮廓也保持同一结构身份。
    inline double normalizedTurn(const std::vector<cv::Point2d> &p, size_t i)
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

    // 六边L/M的RDP偶尔保留一个额外顶点。只修复7→6，删除无贡献的8点组合搜索。
    inline std::vector<std::vector<cv::Point2d> > cornerTopologyPolygons(
        const std::vector<cv::Point> &contour, const std::vector<cv::Point> &simple, double epsilon)
    {
        std::vector<std::vector<cv::Point2d> > result;
        if (simple.size() == 6)
        {
            result.emplace_back(simple.begin(), simple.end());
            return result;
        }
        if (simple.size() != 7)
            return result;
        std::vector<size_t> indices;
        for (auto p:simple)
        {
            auto it = std::find(contour.begin(), contour.end(), p);
            if (it == contour.end())
                return result;
            indices.push_back(size_t(it - contour.begin()));
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
                for (size_t k = begin;; k = (k + 1) % contour.size())
                {
                    if (observed::segmentDistance(contour[k], polygon[i], polygon[(i + 1) % 6]) > epsilon)
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
