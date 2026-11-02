#pragma once
#include <vector>
#include <opencv2/core.hpp>
#include "core/geometry_types.hpp"
#include "vision/types/marker_geometry.hpp"
#include "mark/detector_config.hpp"
namespace mark {
// D05：三个实测 L 观察枚举模型对应，输出候选绑定；资源耗尽仍显式标截断。
std::vector<std::vector<ComponentAssignment>> generateThreeLAssignments(
 const std::vector<ShapeObservation>&,const MarkerGeometry&,const GeometryConfig&,bool &resource_truncated);
// D06：只组装绑定与已有仿射；验证残差留 -1，后续验证步骤负责测量。
GeometryHypothesis buildGeometryHypothesis(const std::vector<ComponentAssignment>&,const cv::Mat&);
// D05→D06：枚举实际拓扑锚点，拟合并交付候选批次；模型只解释观察，不充当真值。
GeometryBatch generateGeometryHypotheses(const std::vector<ShapeObservation>&,const MarkerGeometry&,const GeometryConfig&);
}
