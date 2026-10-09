// 批准的观察层L规则：固定多简化、完整原始弧检查、显式长臂L，输出仅实测点。
#pragma once
#include "core/geometry_types.hpp"
#include "mark/detector_config.hpp"
namespace mark {
// 验证候选声明的唯一凹点；L 类别不能替所有原轮廓凹点提供资格。
bool is_valid_l_topology_candidate(const LTopologyCandidate& candidate);
std::vector<LTopologyCandidate> observeLTopologies(const WhiteComponent&,const GeometryConfig&);
}
