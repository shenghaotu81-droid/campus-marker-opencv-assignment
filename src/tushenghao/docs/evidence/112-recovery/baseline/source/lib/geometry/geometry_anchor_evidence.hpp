#pragma once
#include "core/geometry_types.hpp"
namespace mark {
struct LAnchorTopologySupport {
    std::size_t topology_candidate_index_;
    std::size_t concave_vertex_index_;
};
struct ObservedLAnchor {
    cv::Point2f point_;
    std::size_t source_component_id_;
    std::vector<LAnchorTopologySupport> topology_supports_;
};
struct LAnchorCollection {
    std::vector<ObservedLAnchor> anchors_;
    std::size_t rejected_topology_candidates_{0};
};
LAnchorCollection collect_observed_l_anchors(const ShapeObservation&,std::size_t resolved_source_component_id);
}
