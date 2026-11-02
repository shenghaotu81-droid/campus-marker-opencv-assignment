// 输入已验证三L及真实白片，输出M/S一对一补全分支；不改三L拟合/评分/参数。
#pragma once
#include "vision/types/detection/geometry_types.hpp"
#include "vision/types/marker_geometry.hpp"
#include "vision/config/detector_config.hpp"
namespace mark {
GeometryBatch completeSegmentedAssignments(const GeometryBatch&, const std::vector<WhiteComponent>&,
                                           const MarkerGeometry&, const AssignmentCompletionConfig&);
}
