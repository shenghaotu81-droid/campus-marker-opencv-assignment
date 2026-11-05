// 输入完整原图轮廓、指定两条模型边及fixture/获批预算；输出一个当前观测角证据。
#pragma once
#include "vision/types/detection/corner_types.hpp"
#include "vision/config/detector_config.hpp"

namespace mark
{
    struct EdgePairFitResult
    {
        std::optional<CornerEvidence> evidence;
        std::string reason;
    };

    // 索引优化可能漏掉别名或低分歧义候选；仅验证工具请求时保存全部合格弧与配对。
    struct EdgePairArcTrace
    {
        size_t stage, begin, end;
        unsigned edge_mask;
        cv::Vec4d line;
        std::vector<cv::Point2d> support;
        double mean, maximum, model_mean, model_maximum;
        LineFitModel model;
        int axis;
        double coefficient;
    };

    struct EdgePairPairTrace
    {
        size_t stage;
        std::array<size_t, 4> endpoints;
        cv::Point2d intersection;
        std::vector<cv::Point2d> connector;
        double corner_error, first_extension, second_extension;
    };

    struct EdgePairFitTrace
    {
        std::vector<EdgePairArcTrace> arcs;
        std::vector<EdgePairPairTrace> pairs;
    };

    EdgePairFitResult
    fitObservedEdgePair(const std::vector<cv::Point> &contour,
                        const std::array<std::array<cv::Point2d, 2>, 2> &model_edges_original,
                        const CornerConfig &config);

    // 测试可收集当前调用的弧/配对，不在生产累计路线命中。
    EdgePairFitResult fitObservedEdgePair(const std::vector<cv::Point>&,
        const std::array<std::array<cv::Point2d,2>,2>&, const CornerConfig&, EdgePairFitTrace*);
}
