// block2step5
/*
PreparedFrame
      |
      v
extractWhiteComponents()
      |
      v
WhiteComponent
      |
      v
observeShapes()
      |
      v
ShapeObservation
*/
#pragma once

#include "core/geometry_types.hpp"

#include <vector>

namespace mark
{

    class PreparedFrame;
    class GeometryConfig;

    // 从预处理图像中提取白色连通区域。
    // 输出基础视觉组件，不进行 MARK 几何解释。
    std::vector<WhiteComponent> extractWhiteComponents(
        const PreparedFrame &frame,
        const GeometryConfig &config);

    // 对白色组件进行几何结构观察。
    // 输出简化多边形、凸凹特征和候选类别等中间几何证据。
    std::vector<ShapeObservation> observeShapes(
        const std::vector<WhiteComponent> &components,
        const GeometryConfig &config);

} // namespace mark