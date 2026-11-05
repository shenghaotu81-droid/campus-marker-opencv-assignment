#pragma once
#include "vision/math/observed_geometry.hpp"
#include <opencv2/imgproc.hpp>
namespace mark {
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

    // RDP会同时删除两个近邻顶点，顶点数可能从7直接跳5；补枚举至多两个额外顶点。
    // 仅保留实测轮廓上的顶点，每条合并边覆盖的完整原始弧都必须在同一epsilon上限内。
    // 不改变完整轮廓的边界/面积指标，不据模型投影产生新点，不增加误差预算。
    inline std::vector<std::vector<cv::Point2d>>
    boundedTopologyPolygons(const std::vector<cv::Point> &contour,
                            const std::vector<cv::Point> &simple, size_t target, double epsilon)
    {
        std::vector<std::vector<cv::Point2d>> result;
        if (simple.size() == target)
        {
            result.emplace_back(simple.begin(), simple.end());
            return result;
        }
        if (simple.size() < target || simple.size() > target + 2 || simple.size() > 8)
            return result;
        std::vector<size_t> indices;
        for (auto p : simple)
        {
            auto found = std::find(contour.begin(), contour.end(), p);
            if (found == contour.end())
                return result;
            indices.push_back(found - contour.begin());
        }
        // 闭合RDP维持原轮廓遍历顺序；位掩码最多2^8，候选数有固定上界。
        for (unsigned mask = 0; mask < (1u << simple.size()); ++mask)
        {
            size_t count = 0;
            for (size_t i = 0; i < simple.size(); ++i)
                count += (mask >> i) & 1u;
            if (count != target)
                continue;
            std::vector<size_t> selected;
            std::vector<cv::Point2d> polygon;
            for (size_t i = 0; i < simple.size(); ++i)
                if ((mask >> i) & 1u)
                {
                    selected.push_back(i);
                    polygon.push_back(simple[i]);
                }
            bool bounded = true;
            for (size_t i = 0; i < target && bounded; ++i)
            {
                size_t begin = indices[selected[i]], end = indices[selected[(i + 1) % target]];
                for (size_t k = begin;; k = (k + 1) % contour.size())
                {
                    if (observed::segmentDistance(contour[k], polygon[i],
                                                  polygon[(i + 1) % target]) > epsilon)
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
