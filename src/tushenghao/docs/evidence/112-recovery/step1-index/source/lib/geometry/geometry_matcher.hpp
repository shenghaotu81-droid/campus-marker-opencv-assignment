/* step6:内部流程
ShapeObservation
    |
    v
三 L 组合（C(n,3) + 6 排列）
    |
    v
仿射拟合
    |
    v
GeometryHypothesis
*/
#pragma once

#include <vector>

#include <opencv2/core.hpp>

#include "core/geometry_types.hpp"
#include "core/marker_geometry.hpp"
#include "mark/detector_config.hpp"

namespace mark
{

        /*
         * Step 6 Geometry Hypothesis Generation 总入口。
         *
         * 输入：
         *
         * ShapeObservation:
         *     来自 Step 5 的视觉几何观测。
         *     不包含 MARK ID。
         *
         * MarkerGeometry:
         *     来自 YAML 的模型几何。
         *     只提供候选几何解释依据，
         *     不代表 detector 已经知道结果。
         *
         * GeometryConfig:
         *     几何推理相关参数。
         *
         * 输出：
         *
         * GeometryBatch:
         *     当前帧所有通过几何推理得到的假设。
         */
        GeometryBatch generateGeometryHypotheses(
            const std::vector<ShapeObservation> &observations,
            const MarkerGeometry &model_geometry,
            const GeometryConfig &config);

        /*
         * Step 6.2：
         *
         * 从 ShapeObservation 中寻找三个互相兼容的 L。
         *
         * 流程：
         *
         * 1. 过滤 supported_classes_ 包含 "L" 的观测。
         * 2. 对观测 L 做 C(n,3) 组合。
         * 3. 对模型中的三个 L 做 3! = 6 种排列。
         *
         * 输出：
         *
         * ComponentAssignment 集合。
         *
         * 注意：
         * ComponentAssignment 是候选几何解释，
         * 不是 detector 已知 ID。
         */
        std::vector<std::vector<ComponentAssignment>>
        generateSixComponentCombinations(
            const std::vector<ShapeObservation> &observations,
            const MarkerGeometry &model_geometry,
            const GeometryConfig &config,
            bool &resource_truncated);

        /*
         * Step 6.3：
         *
         * 根据模型锚点和观测锚点，
         * 拟合二维仿射变换。
         *
         * 方向固定：
         *
         * model coordinate
         *          |
         *          v
         * working image coordinate
         *
         * 返回：
         *
         * 2x3 CV_64F affine matrix。
         */
        cv::Mat fitModelToImageAffine(
            const std::vector<ComponentAssignment> &assignment,
            const std::vector<ShapeObservation> &observations,
            const MarkerGeometry &model_geometry);

        /*
         * Step 6.4：
         *
         * 将通过几何验证的结果封装为 GeometryHypothesis。
         *
         * 保存：
         *
         * - assignments_
         * - affine_transform_
         * - validation_residual_
         * - completeness_
         * - evidence_
         */
        GeometryHypothesis buildGeometryHypothesis(
            const std::vector<ComponentAssignment> &assignment,
            const cv::Mat &affine_transform,
            const std::vector<ShapeObservation> &observations,
            const MarkerGeometry &model_geometry);

} // namespace mark

/* 当前设计路线：ID 是推理结果，不是输入
image
 |
 v
WhiteComponent
 |
 v
ShapeObservation
 |
 v
candidate assignment
 |
 v
geometry hypothesis
 |
 v
得到解释:
"这个白块集合最符合模型 L0/L1/M..."
*/