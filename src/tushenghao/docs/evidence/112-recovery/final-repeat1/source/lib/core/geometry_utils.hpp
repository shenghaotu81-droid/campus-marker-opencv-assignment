// 几何计算层
#pragma once

#include "core/marker_geometry.hpp"

#include <opencv2/core.hpp>

namespace mark
{

    // 计算 polygon 面积。
    // 使用 vertices 描述的多边形计算实际几何面积。
    double computePolygonArea(
        const GeometryPolygon &polygon);

    // 计算 polygon 几何中心。
    // 返回 vertices 构成多边形的 centroid。
    cv::Point2f computeCentroid(
        const GeometryPolygon &polygon);

    // 计算 anchor 相对于 polygon 几何中心的偏移。
    // 返回:
    // anchor - centroid
    cv::Point2f computeAnchorOffset(
        const GeometryPolygon &polygon);

    // 计算 polygon 的轴对齐包围盒。
    // 用于后续 ROI / 区域过滤等操作。
    cv::Rect2f computeBoundingBox(
        const GeometryPolygon &polygon);

} // namespace mark