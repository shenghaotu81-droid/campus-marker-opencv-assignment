/* 测试覆盖范围：图纸加载
YAML
 ↓
loadMarkerGeometry()
 ↓
MarkerGeometry
 ↓
validateMarkerGeometry()
测的是"图纸能不能正确读进程序"。
具体三件事：
1. YAML 文件能打开，6 个白块都在（L0、L2、L3、M1、S1a、S1b）
2. 每个白块的数据完整（有编号、有顶点、有面积）
3. L0 的数值跟批准的表对得上（6 个顶点、锚点 8,8、面积 416）
不测算法，只测"图纸加载"这个环节。
*/
#include "core/marker_geometry.hpp"

#include <cassert>
#include <iostream>
#include <set>
#include <filesystem>

int main()
{
    /*
    auto geometry = mark::loadMarkerGeometry(                // 代码在 IDE 里运行正常 ≠ CTest 正常;测试不能依赖当前 shell 目录，路径应该由 CMake 或测试配置管理。
        "../../src/tushenghao/config/marker_geometry.yaml"); // Working directory trap，工作目录陷阱。测试依赖了运行时的当前目录，换个地方跑就找不到文件。
        // 解决方案：1. CMake 配置测试时，设置工作目录为项目根目录；2. 测试代码里用绝对路径；3. 测试代码里用相对路径，但相对于 CMake 配置的工作目录。
    */

    auto geometry = mark::loadMarkerGeometry(
        ([]()
         {
    namespace fs = std::filesystem;
    // __FILE__ = .../src/tushenghao/tests/geometry_model_test.cpp
    // parent_path x2 = .../src/tushenghao
    fs::path base = fs::path(__FILE__).parent_path().parent_path();
    return (base / "config" / "marker_geometry.yaml").string(); })());

    mark::validateMarkerGeometry(
        geometry);

    // 检查 polygon 数量
    assert(
        geometry.polygons.size() == 6);

    // 检查所有 polygon id 是否完整
    std::set<std::string> expected_ids{
        "L0",
        "L2",
        "L3",
        "M1",
        "S1a",
        "S1b"};

    std::set<std::string> actual_ids;

    for (const auto &polygon : geometry.polygons)
    {
        actual_ids.insert(
            polygon.id);
    }

    assert(
        actual_ids == expected_ids);

    // 检查每个 polygon 基本字段
    for (const auto &polygon : geometry.polygons)
    {
        assert(
            !polygon.id.empty());

        assert(
            polygon.vertices.size() >= 3);

        assert(
            polygon.area > 0.0);
    }

    // 检查 L0 具体字段
    const auto &l0 =
        geometry.polygons[0];

    assert(
        l0.id == "L0");

    assert(
        l0.vertices.size() == 6);

    assert(
        l0.anchor.x == 8.0f);

    assert(
        l0.anchor.y == 8.0f);

    assert(
        l0.area == 416.0);

    std::cout
        << "geometry_model_test passed\n";

    return 0;
}