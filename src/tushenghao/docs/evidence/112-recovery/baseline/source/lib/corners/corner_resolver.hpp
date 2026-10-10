#pragma once

#include "corners/corner_types.hpp"
#include "mark/detector_config.hpp"
#include "core/geometry_types.hpp"
#include "core/marker_geometry.hpp"
#include "core/prepared_frame.hpp"

namespace mark
{

    /**
     * @brief 根据当前几何假设恢复四个物理角
     *
     * 输入：
     * - 当前帧观测；
     * - Block 2 几何假设；
     * - MARK 模型；
     * - 角点恢复配置。
     *
     * 输出：
     * - 成功：CornerMeasurement + 四个 CornerEvidence；
     * - 失败：FAILED + 中文拒绝原因。
     *
     * 不负责：
     * - 屏幕排序；
     * - 语义归并；
     * - 稳定输出。
     */
    CornerResolution resolveObservedCorners(
        const PreparedFrame &frame,
        const GeometryHypothesis &hypothesis,
        const MarkerGeometry &model,
        const CornerConfig &config);

} // namespace mark