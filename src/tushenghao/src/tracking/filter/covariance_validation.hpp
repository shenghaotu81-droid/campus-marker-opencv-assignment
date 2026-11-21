// 输入协方差，输出有限／对称／正定结论；不依赖滤波状态。
#pragma once
#include <opencv2/core.hpp>

namespace mark::tracking::detail
{
    // 工具函数：检查矩阵是不是正定（用来验算 P 的正定性，不通过就抛 TRACK_COVARIANCE_NOT_POSITIVE_DEFINITE）
    bool positive_definite(const cv::Mat &);
}
