// 输入已验证三L及真实白片，输出M/S一对一补全分支；不改三L拟合/评分/参数。
#pragma once
#include "core/geometry_types.hpp"
#include "core/marker_geometry.hpp"
#include "mark/detector_config.hpp"
namespace mark {
GeometryBatch completeSegmentedAssignments(const GeometryBatch&, const std::vector<WhiteComponent>&,
                                           const MarkerGeometry&, const AssignmentCompletionConfig&);
}
