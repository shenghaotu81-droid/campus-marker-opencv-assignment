// step7:独立验证（输入输出接口:验证+输出）
#pragma once

#include "core/geometry_types.hpp"
#include "core/marker_geometry.hpp"
#include "mark/detector_config.hpp"

#include <vector>

namespace mark
{

    /*
     * Step 7 几何验证模块。
     *
     * 输入：
     *   Step 6 生成的 GeometryBatch
     *   MARK 模型几何 MarkerGeometry
     *   当前帧白块观测 WhiteComponent
     *   配置阈值 GeometryConfig
     *
     * 输出：
     *   验证、过滤、打标后的 GeometryBatch
     *
     * 设计约束：
     *
     * 1. 不修改 geometry_matcher.cpp。
     *    Step 6 已冻结。
     *
     * 2. 不重新生成 hypothesis。
     *
     * 3. 不排名。
     *
     * 4. 不从多个 hypothesis 中强选唯一答案。
     *    所有通过验证的解释全部保留，
     *    交给 Block 3 后续处理。
     *
     * 5. 输入 batch 只读。
     *    返回新的 GeometryBatch。
     */
    GeometryBatch validateGeometryBatch(
        const GeometryBatch &batch,
        const MarkerGeometry &geometry,
        const std::vector<WhiteComponent> &components,
        const GeometryConfig &config);

} // namespace mark