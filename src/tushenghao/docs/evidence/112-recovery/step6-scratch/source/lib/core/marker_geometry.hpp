// 把 MARK 灯的“图纸”翻译成程序能理解的数据结构(图纸模型层建模)
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

namespace mark
{
    // 一个白色几何块（L/M/S）的模型
    // 几何多边形模型：描述一个 MARK 几何组件的顶点和锚点。
    struct GeometryPolygon
    {
        // 组件编号，例如 L0、L1、M、S。
        std::string id;

        // 多边形顶点，顺序按照模型定义保存。
        std::vector<cv::Point2f> vertices;

        // 该组件对应的模型锚点。(关键点)
        cv::Point2f anchor;

        // 模型面积，由配置加载或校验阶段计算。(图纸模型面积:模型理论面积,实际图片因为旋转、缩放、分割误差，会不同)
        double area = 0.0;
    };

    // 整个 MARK 图纸,里面装很多白块(三个 L + 一个 M + 两个 S)
    // MARK 整体几何模型：保存所有组件的几何描述。
    struct MarkerGeometry
    {
        // 几何模型版本号。
        int schema_version = 0;

        // MARK 组成组件列表。(多个白色组件)
        std::vector<GeometryPolygon> polygons;
    };

    // 从 YAML 文件加载 MARK 几何模型。
    MarkerGeometry loadMarkerGeometry(
        const std::filesystem::path &path
    );

    // 校验 MARK 几何模型是否合法。
    void validateMarkerGeometry(
        const MarkerGeometry &geometry
    );

} // namespace mark