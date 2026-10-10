// 只校验显式预算；缺配置/非法配置是程序配置错误，不能包装成视觉empty。
#pragma once
#include "mark/detector_config.hpp"
#include "core/config_error.hpp"
#include <cmath>
namespace mark {
inline void validateCornerObservationBudget(const CornerConfig& config) {
    if (!config.observation_budget_) throw ConfigError("CORNER_BUDGET_NOT_CONFIGURED: G3/G4未批准配置");
    const auto& b=*config.observation_budget_;
    for (double v:{b.max_edge_direction_diff_deg,b.max_edge_position_distance_px,
                   b.max_component_mapping_distance_px,b.turn_trim_distance_px,
                   b.max_turn_connection_length_px,b.max_support_extension_px,b.min_support_span_px,
                   config.max_line_fit_error_,config.max_corner_error_,config.semantic_geometry_threshold_})
        if (!std::isfinite(v)||v<0) throw ConfigError("CORNER_BUDGET_INVALID: 预算须非负有限");
    if (b.max_edge_direction_diff_deg>90 || b.min_support_span_px<=0 || config.min_line_points_<3 ||
        !std::isfinite(config.approximation_epsilon_) || config.approximation_epsilon_<=0 ||
        !std::isfinite(config.min_intersection_angle_deg_) || config.min_intersection_angle_deg_<=0 ||
        config.min_intersection_angle_deg_>90)
        throw ConfigError("CORNER_BUDGET_INVALID: 点数/跨度/简化/病态角预算非法");
}
}
