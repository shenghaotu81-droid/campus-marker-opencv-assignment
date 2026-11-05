// 优化必须与完整 reference 对账；此入口仅链接进测试，不进入 Detector 库。
#pragma once
#include "corners/fitting/corner_edge_fit.hpp"

namespace mark::edge_fit_reference
{
    EdgePairFitResult fitObservedEdgePair(const std::vector<cv::Point> &contour,
                                          const std::array<std::array<cv::Point2d, 2>, 2> &edges,
                                          const CornerConfig &config,
                                          EdgePairFitTrace *trace = nullptr);
}
