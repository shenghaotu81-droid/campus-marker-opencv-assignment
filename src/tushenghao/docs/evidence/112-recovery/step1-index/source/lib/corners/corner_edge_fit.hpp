// 输入完整原图轮廓、指定两条模型边及fixture/获批预算；输出一个当前观测角证据。
#pragma once
#include "corners/corner_types.hpp"
#include "mark/detector_config.hpp"

namespace mark
{
    struct EdgePairFitResult
    {
        std::optional<CornerEvidence> evidence;
        std::string reason;
    };

    EdgePairFitResult
    fitObservedEdgePair(const std::vector<cv::Point> &contour,
                        const std::array<std::array<cv::Point2d, 2>, 2> &model_edges_original,
                        const CornerConfig &config);
}
