// 检查运行期协方差有限、对称且正定，防止数值异常进入预测和更新。
#include "filter/covariance_validation.hpp"
#include <algorithm>
#include <limits>
namespace mark::tracking::detail
{
    bool positive_definite(const cv::Mat &m)
    {
        if (m.empty() || m.type() != CV_64F || m.rows != m.cols || !cv::checkRange(m))
            return false;
        const double scale = std::max(1.0, cv::norm(m, cv::NORM_INF));
        const double roundoff = std::numeric_limits<double>::epsilon() * m.rows * scale;
        if (cv::norm(m - m.t(), cv::NORM_INF) > roundoff)
            return false;
        cv::Mat eigenvalues;
        return cv::eigen(m, eigenvalues) && eigenvalues.at<double>(m.rows - 1) > 0;
    }
}
