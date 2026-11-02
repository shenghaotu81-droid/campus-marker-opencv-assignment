// Q04：文件读取与合法性核验；输入显式路径／模型，输出只读模型或原异常。
#pragma once
#include "vision/types/marker_geometry.hpp"
#include <filesystem>
namespace mark
{
    // 从 YAML 文件加载 MARK 几何模型。
    MarkerGeometry loadMarkerGeometry(
        const std::filesystem::path &path
    );

    // 校验 MARK 几何模型是否合法。
    void validateMarkerGeometry(
        const MarkerGeometry &geometry
    );

} // namespace mark
