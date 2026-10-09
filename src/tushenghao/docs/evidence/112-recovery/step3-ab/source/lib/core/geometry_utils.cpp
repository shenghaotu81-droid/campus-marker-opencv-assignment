// 几何计算层
#include "core/geometry_utils.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <stdexcept>

namespace mark
{

    // 计算 polygon 的实际几何面积。
    // 不直接使用配置文件中的 area 字段(模型数据)，而是根据 vertices 重新计算，
    // 保证模型数据与实际几何描述保持一致。
    double computePolygonArea(const GeometryPolygon &polygon)
    {
        if (polygon.vertices.size() < 3)
        {
            throw std::runtime_error(
                "polygon must have at least 3 vertices"); // 至少需要三个顶点才能构成多边形
        }

        return std::abs(cv::contourArea(
            polygon.vertices)); // 使用 OpenCV contourArea 计算多边形面积，返回绝对值以确保非负
    }

    // 计算 polygon 的几何中心。（整个形状的平均中心）
    // 使用 OpenCV moments 计算 centroid，避免手动实现多边形质心公式。
    // 对于零面积 polygon，质心没有数学意义，因此直接报错。
    cv::Point2f computeCentroid(const GeometryPolygon &polygon)
    {
        if (polygon.vertices.size() < 3)
        {
            throw std::runtime_error(
                "polygon must have at least 3 vertices"); // 至少需要三个顶点才能构成多边形
        }

        cv::Moments moments = cv::moments(
            polygon
                .vertices); // 使用 OpenCV moments 计算多边形的空间矩，moments.m00 为面积，moments.m10 和 moments.m01 为一阶矩（x,y方向加权：所有面积元素的 x 坐标贡献总和，y 坐标贡献总和）

        if (moments.m00 == 0)
        {
            throw std::runtime_error(
                "cannot compute centroid of zero area polygon"); // 零面积多边形没有定义质心（如三点共线，防止后面报错）
        }

        return cv::Point2f(static_cast<float>(moments.m10 / moments.m00),
                           static_cast<float>(moments.m01 / moments.m00)); // 质心计算公式
    }

    // 计算 anchor 相对于 polygon 几何中心的偏移。
    // 保留模型定义中的 anchor 信息，用于后续坐标转换或姿态计算。
    cv::Point2f computeAnchorOffset(const GeometryPolygon &polygon)
    {
        return polygon.anchor - computeCentroid(polygon); // 返回一个 Point2f 表示偏移向量
    }

    // 计算 polygon 的轴对齐包围盒。
    // 通过遍历 vertices 获取边界范围，生成后续 ROI 或区域过滤所需的矩形。
    cv::Rect2f computeBoundingBox(const GeometryPolygon &polygon)
    {
        if (polygon.vertices.empty())
        {
            throw std::runtime_error(
                "cannot compute bounding box of empty polygon"); // 空多边形没有定义包围盒
        }

        // 初始化边界值为第一个顶点的坐标
        float min_x = polygon.vertices[0].x;
        float max_x = polygon.vertices[0].x;
        float min_y = polygon.vertices[0].y;
        float max_y = polygon.vertices[0].y;

        //
        for (const auto &point : polygon.vertices)
        {
            min_x = std::min(min_x, point.x);
            max_x = std::max(max_x, point.x);

            min_y = std::min(min_y, point.y);
            max_y = std::max(max_y, point.y);
        }

        // 返回一个 Rect2f 表示包围盒，左上角为 (min_x, min_y)，宽度为 (max_x - min_x)，高度为 (max_y - min_y)
        return cv::Rect2f(min_x, min_y, max_x - min_x, max_y - min_y);
    }

} // namespace mark