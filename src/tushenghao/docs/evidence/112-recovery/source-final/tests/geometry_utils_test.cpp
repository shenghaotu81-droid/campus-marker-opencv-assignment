/* 几何计算测试：
用一个 30x30 的正方形做基准，因为结果好算：
面积是不是 900
中心是不是 (15,15)
锚点偏移是不是 (-7,-7)
包围盒是不是 (0,0,30,30)
就是验证数学算对了，没算错。
*/
// 测试几何计算模块：面积、中心点、anchor 偏移、包围盒。

#include "core/geometry_utils.hpp"

#include <cassert>                 // 断言（assertion）库：在程序运行时检查一个“你认为一定成立”的条件，如果不成立，就直接报错停止（测试不可能发生的内部逻辑错误）
#include <cmath>
#include <iostream>

int main()
{
    mark::GeometryPolygon polygon;

    polygon.id = "test";

    // 定义一个 30x30 的正方形 polygon，顶点按顺时针顺序排列。
    polygon.vertices =
        {
            {0, 0},
            {30, 0},
            {30, 30},
            {0, 30}};

    polygon.anchor = {8, 8};

    // 测试 polygon 面积。
    // 使用顶点重新计算，确认几何计算结果正确。
    double area =
        mark::computePolygonArea(polygon);

    assert(std::abs(area - 900.0) < 1e-5);

    // 测试 polygon 几何中心。
    // 对于规则矩形，中心应位于四个顶点的中间。
    cv::Point2f centroid =
        mark::computeCentroid(polygon);

    assert(std::abs(centroid.x - 15.0f) < 1e-5);
    assert(std::abs(centroid.y - 15.0f) < 1e-5);

    // 测试 anchor 相对于中心的偏移。
    // anchor(8,8) 相对于 centroid(15,15) 应为 (-7,-7)。
    cv::Point2f offset =
        mark::computeAnchorOffset(polygon);

    assert(std::abs(offset.x + 7.0f) < 1e-5);
    assert(std::abs(offset.y + 7.0f) < 1e-5);

    // 测试轴对齐包围盒。
    cv::Rect2f bbox =
        mark::computeBoundingBox(polygon);

    assert(std::abs(bbox.x - 0.0f) < 1e-5);
    assert(std::abs(bbox.y - 0.0f) < 1e-5);
    assert(std::abs(bbox.width - 30.0f) < 1e-5);
    assert(std::abs(bbox.height - 30.0f) < 1e-5);

    std::cout << "geometry_utils_test passed\n";

    return 0;
}