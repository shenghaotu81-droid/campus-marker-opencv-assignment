// 输入真实点/有限线段，输出纯几何距离/映射；非法矩阵明确失败，不触及图像或状态。
#pragma once
#include <opencv2/core.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace mark::observed
{
    // 判断像素/变换有限，避免NaN越过后续比较。
    inline bool finite(cv::Point2d p)
    {
        return std::isfinite(p.x) && std::isfinite(p.y);
    }

    // 原三L矩阵只消费不重估；整体镜像不能混同局部模板绕向。
    inline bool validAffine(const cv::Mat &m)
    {
        return m.rows == 2 && m.cols == 3 && m.type() == CV_64FC1 && cv::checkRange(m) &&
               (m.at<double>(0, 0) * m.at<double>(1, 1) - m.at<double>(0, 1) * m.at<double>(1, 0)) >
                   0;
    }

    // 模型投影只能引导验证，不作为任何有效角点输出。
    inline cv::Point2d project(const cv::Mat &m, cv::Point2d p)
    {
        return {m.at<double>(0, 0) * p.x + m.at<double>(0, 1) * p.y + m.at<double>(0, 2),
                m.at<double>(1, 0) * p.x + m.at<double>(1, 1) * p.y + m.at<double>(1, 2)};
    }

    // 点到有限线段；裁剪的是内部投影参数，不是检测角坐标。
    inline double segmentDistance(cv::Point2d p, cv::Point2d a, cv::Point2d b)
    {
        auto v = b - a;
        double n = v.dot(v);
        if (!(n > 0))
            return cv::norm(p - a);
        return cv::norm(p - (a + v * std::clamp((p - a).dot(v) / n, 0.0, 1.0)));
    }

    // 原轮廓相邻点按有限段度量，保留真实连接；不对筛掉的点造新闭合边。
    inline double boundaryDistance(cv::Point2d p, const std::vector<cv::Point2d> &ring,
                                   bool closed = true)
    {
        double best = std::numeric_limits<double>::infinity();
        for (size_t i = 1; i < ring.size(); ++i)
            best = std::min(best, segmentDistance(p, ring[i - 1], ring[i]));
        if (closed && ring.size() > 1)
            best = std::min(best, segmentDistance(p, ring.back(), ring.front()));
        return best;
    }

    // 无向锐夹角同时覆盖平行/反平行方向，退化边返回非有限指标。
    inline double angleDeg(cv::Point2d a, cv::Point2d b)
    {
        double n = cv::norm(a) * cv::norm(b);
        if (!(n > 0))
            return std::numeric_limits<double>::infinity();
        return std::acos(std::clamp(std::abs(a.dot(b)) / n, 0.0, 1.0)) * 180 / CV_PI;
    }
}
