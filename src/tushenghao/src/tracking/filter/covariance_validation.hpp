// 输入协方差，输出有限／对称／正定结论；不依赖滤波状态。
#pragma once
#include <opencv2/core.hpp>

namespace mark::tracking::detail
{
    bool positive_definite(const cv::Mat &);
}
