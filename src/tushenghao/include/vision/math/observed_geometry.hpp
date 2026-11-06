// 点到有限线段；只含匹配主链实际使用的几何原语。
#pragma once
#include <opencv2/core.hpp>
#include <algorithm>

namespace mark::observed
{
    // 点到有限线段；裁剪的是内部投影参数，不是检测角坐标。
    inline double segmentDistance(cv::Point2d p, cv::Point2d a, cv::Point2d b)
    {
        auto v = b - a;
        double n = v.dot(v);
        if (!(n > 0))
            return cv::norm(p - a);
        return cv::norm(p - (a + v * std::clamp((p - a).dot(v) / n, 0.0, 1.0)));
    }

}
