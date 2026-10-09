#include "geometry/geometry_anchor_evidence.hpp"
#include "geometry/geometry_l_topology.hpp"
#include <algorithm>

namespace mark
{
    LAnchorCollection collect_observed_l_anchors(const ShapeObservation &observation,
                                                 std::size_t resolved_source_component_id)
    {
        LAnchorCollection result;
        // 原错误把 L 候选类别借给所有裸凹点。逐候选验证只授权真实六边 L 的凹角，
        // 不从 turns/anchor_vertex_index 回填；同点合并拟合坐标但保留全部独立来源。
        for (std::size_t i = 0; i < observation.l_topology_candidates_.size(); ++i)
        {
            const auto &candidate = observation.l_topology_candidates_[i];
            if (!is_valid_l_topology_candidate(candidate))
            {
                ++result.rejected_topology_candidates_;
                continue;
            }
            auto vertex = candidate.concave_vertex_indices_[0];
            auto point = candidate.polygon_[vertex];
            auto found = std::find_if(result.anchors_.begin(), result.anchors_.end(),
                                      [&](const auto &a)
                                      {
                                          return a.point_ == point;
                                      });
            if (found == result.anchors_.end())
                result.anchors_.push_back({point, resolved_source_component_id, {{i, vertex}}});
            else
                found->topology_supports_.push_back({i, vertex});
        }
        return result;
    }
}
