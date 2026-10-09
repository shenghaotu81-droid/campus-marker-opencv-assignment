#include "core/marker_geometry.hpp"

#include <set>
#include <stdexcept>

#include <opencv2/imgproc.hpp>
#include <opencv2/core.hpp>

namespace mark
{
    // 加载标记几何信息
    /* 实现 loadMarkerGeometry():
    不直接把 FileNode 留在结构体里
        * YAML
            |
            | FileStorage
            v
         cv::FileNode
            |
            v
            MarkerGeometry
    */
    MarkerGeometry loadMarkerGeometry(
        const std::filesystem::path &path)
    {
        cv::FileStorage fs(
            path.string(),
            cv::FileStorage::READ);

        if (!fs.isOpened())
        {
            throw std::runtime_error(
                "cannot open geometry file: " + path.string()); // 错误: 无法打开几何文件
        }

        MarkerGeometry geometry;

        fs["schema_version"] >> geometry.schema_version;

        if (geometry.schema_version != 1)
        {
            throw std::runtime_error(
                "unsupported geometry schema version"); // 错误: 不支持的几何模型版本
        }

        cv::FileNode polygons_node = fs["polygons"];

        if (polygons_node.empty())
        {
            throw std::runtime_error(
                "missing polygons"); // 错误: 缺少多边形数据
        }

        // 读取每个多边形组件
        for (const auto &node : polygons_node)
        {
            GeometryPolygon polygon;

            node["id"] >> polygon.id;

            cv::FileNode vertices_node = node["vertices"];

            if (vertices_node.empty())
            {
                throw std::runtime_error(
                    "missing polygon vertices");   // 错误: 缺少多边形顶点数据
            }                             

            for (const auto &point : vertices_node)
            {
                float x = static_cast<float>(point[0]);
                float y = static_cast<float>(point[1]);

                polygon.vertices.emplace_back(x, y);
            }

            cv::FileNode anchor_node = node["anchor"];

            if (anchor_node.empty())
            {
                throw std::runtime_error(
                    "missing polygon anchor"); // 错误: 缺少多边形锚点数据
            }

            polygon.anchor.x = static_cast<float>(
                anchor_node[0]);

            polygon.anchor.y = static_cast<float>(
                anchor_node[1]);

            node["area"] >> polygon.area;       // 读取多边形面积

            geometry.polygons.push_back(polygon);     
        }

        return geometry;
    }

    // 验证标记几何信息的有效性（加载契约校验）
    void validateMarkerGeometry(
        const MarkerGeometry &geometry)
    {
        if (geometry.schema_version != 1)
        {
            throw std::runtime_error(
                "invalid geometry schema version");// 错误: 无效的几何模型版本
        }                    

        if (geometry.polygons.empty())
        {
            throw std::runtime_error(
                "geometry polygons are empty");// 错误: 几何模型多边形数据为空
        }

        // 一个“已经出现过的编号表
        std::set<std::string> ids;        // 编号唯一检查（保证每一个 GeometryPolygon 的 id 不重复）：set 天生保证一个元素只能出现一次

        for (const auto &polygon : geometry.polygons)
        {
            if (polygon.id.empty())
            {
                throw std::runtime_error(
                    "polygon id is empty");// 错误: 多边形编号为空
            }

            if (!ids.insert(polygon.id).second)    // ids.insert(polygon.id)返回pair<iterator, bool>类似{插入位置,是否成功}
            {
                throw std::runtime_error(
                    "duplicate polygon id: " + polygon.id);// 错误: 多边形编号重复
            }

            if (polygon.vertices.size() < 3)
            {
                throw std::runtime_error(
                    "polygon has fewer than 3 vertices: " + polygon.id);// 错误: 多边形顶点数量小于3
            }

            if (polygon.area <= 0.0)
            {
                throw std::runtime_error(
                    "polygon area is not positive: " + polygon.id);// 错误: 多边形面积不是正数
            }
        }
    }

} // namespace mark