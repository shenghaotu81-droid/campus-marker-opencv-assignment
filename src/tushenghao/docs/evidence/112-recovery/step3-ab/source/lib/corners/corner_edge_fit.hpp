// 输入完整原图轮廓、指定两条模型边及fixture/获批预算；输出一个当前观测角证据。
#pragma once
#include "corners/corner_types.hpp"
#include "mark/detector_config.hpp"

namespace mark
{
    // 旧路径成功必须先返回；策略属于单次拟合实例，不依赖环境变量或公开 YAML。
    enum class EdgePairFitStrategy
    {
        OriginalOnly,
        LinearRecovery,
        PeriodicRecovery,
        FastRecovery
    };

    struct EdgePairFitResult
    {
        std::optional<CornerEvidence> evidence;
        std::string reason;
    };

    EdgePairFitResult
    fitObservedEdgePair(const std::vector<cv::Point> &contour,
                        const std::array<std::array<cv::Point2d, 2>, 2> &model_edges_original,
                        const CornerConfig &config);

    // 内部 reference 入口用于逐步验收；普通消费者仍使用原三参数签名。
    EdgePairFitResult
    fitObservedEdgePair(const std::vector<cv::Point> &contour,
                        const std::array<std::array<cv::Point2d, 2>, 2> &model_edges_original,
                        const CornerConfig &config, EdgePairFitStrategy strategy);
}
