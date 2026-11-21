// 检查矩阵有限、近似对称且严格正定。
#include "filter/covariance_validation.hpp"
#include <algorithm>
#include <limits>

namespace mark::tracking::detail
{
    bool positive_definite(const cv::Mat &m)
    {
        if (m.empty() || m.type() != CV_64F || m.rows != m.cols || !cv::checkRange(m))
            return false;

        // 按矩阵量级设置浮点对称性容差。
        const double scale = std::max(1.0, cv::norm(m, cv::NORM_INF));

        const double roundoff = std::numeric_limits<double>::epsilon() * m.rows * scale;

        if (cv::norm(m - m.t(), cv::NORM_INF) > roundoff)
            return false;

        cv::Mat eigenvalues;

        // 特征值按降序排列。最后一项必须大于零，Mat::at 的断言依构建模式而异。
        return cv::eigen(m, eigenvalues) && eigenvalues.at<double>(m.rows - 1) > 0;
    }
}
