// 输入组件/指定模型片/可信三L中位面积，输出未应用误差上限的原始工作图指标。
// 检测与C测量共用同一尺度、边界采样、循环方向/凹凸定义，避免两套统计口径。
#pragma once
#include "core/geometry_types.hpp"
#include "core/marker_geometry.hpp"
#include "core/observed_geometry_utils.hpp"
#include <opencv2/imgproc.hpp>

namespace mark
{
    struct AssignmentMatchMetrics
    {
        bool valid = false, topology_valid = false;
        double selected_epsilon = 0;
        size_t observed_vertices = 0;
        double boundary_distance = INFINITY, direction_diff = INFINITY,
               relative_area_error = INFINITY;
    };

    // 有限段按显式步长采样；包括端点。距离是1-Lipschitz，连续上界另加step/2。
    inline double sampledBoundaryDistance(const std::vector<cv::Point2d> &a,
                                          const std::vector<cv::Point2d> &b, double step)
    {
        double worst = 0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            auto next = a[(i + 1) % a.size()];
            double n = std::ceil(cv::norm(next - a[i]) / step);
            if (!std::isfinite(n) || n > std::numeric_limits<int>::max())
                return INFINITY;
            for (int k = 0; k <= static_cast<int>(n); ++k)
            {
                auto p = n > 0 ? a[i] + (next - a[i]) * (k / n) : a[i];
                worst = std::max(worst, observed::boundaryDistance(p, b));
            }
        }
        return worst;
    }

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

    // 简化规则/采样步长是输入条件；不使用待求边界、方向、面积门限过滤数据。
    inline AssignmentMatchMetrics measureAssignmentMatch(const WhiteComponent &c,
                                                         const GeometryPolygon &part,
                                                         const cv::Mat &affine, double reference,
                                                         double model_reference, double epsilon,
                                                         double step)
    {
        AssignmentMatchMetrics m;
        if (c.contour_.size() < 3 || c.touches_border_ || !std::isfinite(c.area_) || c.area_ <= 0 ||
            !observed::validAffine(affine) || !std::isfinite(reference) || reference <= 0 ||
            !std::isfinite(model_reference) || model_reference <= 0 || !std::isfinite(epsilon) ||
            epsilon <= 0 || !std::isfinite(step) || step <= 0)
            return m;
        std::vector<cv::Point2d> expected, actual(c.contour_.begin(), c.contour_.end());
        for (auto p : part.vertices)
        {
            auto q = observed::project(affine, p);
            if (!observed::finite(q))
                return m;
            expected.push_back(q);
        }
        if (expected.size() < 3)
            return m;
        m.valid = true;
        m.relative_area_error = std::abs(c.area_ / reference - part.area / model_reference);
        m.boundary_distance = std::max(sampledBoundaryDistance(actual, expected, step),
                                       sampledBoundaryDistance(expected, actual, step));
        // 一个固定epsilon无法同时容纳小图细M与大图圆角。配置仍为简化上限，
        // 保留原五比例(1/3..1)，新增固定1/4细候选及有界额外顶点省略，拓扑可用对应取最小方向差。
        // 实际整条轮廓的有限边界与面积仍独立校验，不能由简化抹掉缺边/错误身份。
        // 1/4候选保留细M的一工作像素凹口；原五比例全部保留，epsilon上限不变。
        for (double fraction : {.25, 1. / 3, .5, 2. / 3, 5. / 6, 1.})
        {
            const double trial_epsilon = epsilon * fraction;
            std::vector<cv::Point> simple;
            cv::approxPolyDP(c.contour_, simple, trial_epsilon, true);
            for (const auto &polygon :
                 boundedTopologyPolygons(c.contour_, simple, expected.size(), epsilon))
                for (size_t start = 0; start < polygon.size(); ++start)
                    for (int direction : {1, -1})
                    {
                        bool topology = true;
                        double error = 0;
                        for (size_t i = 0; i < expected.size(); ++i)
                        {
                            auto index = [&](int offset)
                            {
                                int n = static_cast<int>(polygon.size());
                                return static_cast<size_t>(
                                    (static_cast<int>(start) + direction * offset + n * 2) % n);
                            };
                            size_t j = index(static_cast<int>(i)),
                                   next = index(static_cast<int>(i + 1));
                            if (normalizedTurn(expected, i) * normalizedTurn(polygon, j) <= 0)
                            {
                                topology = false;
                                break;
                            }
                            error = std::max(error,
                                             observed::angleDeg(
                                                 expected[(i + 1) % expected.size()] - expected[i],
                                                 polygon[next] - polygon[j]));
                        }
                        if (topology && error < m.direction_diff)
                        {
                            m.topology_valid = true;
                            m.direction_diff = error;
                            m.observed_vertices = polygon.size();
                            m.selected_epsilon =
                                polygon.size() == simple.size() ? trial_epsilon : epsilon;
                        }
                    }
        }
        return m;
    }
}
