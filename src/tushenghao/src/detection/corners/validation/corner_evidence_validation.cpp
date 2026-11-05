// 生产只检查来源、完整性和数值合法性；claimed值复算移入测试。
#include "corners/validation/corner_evidence_validation.hpp"
#include "vision/types/marker_geometry.hpp"
#include "vision/math/observed_geometry.hpp"
namespace mark {
bool validateCornerEvidence(const CornerEvidence &e, cv::Point2d corner, int index,
    const CornerConfig &config, std::string &reason) {
    auto fail=[&](const char *s) { reason=s; return false; };
    if (!config.observation_budget_) return fail("CORNER_BUDGET_NOT_CONFIGURED");
    if (index<0 || index>=4 || e.truncated_) return fail("TRUNCATED_OR_INVALID_CORNER");
    const auto &binding=model_corner_contract[index];
    if (!e.original_observation_ || e.stable_id_.empty() ||
        e.component_id_==std::numeric_limits<size_t>::max() || int(e.physical_corner_)!=index ||
        e.model_vertex_id_!=binding.vertex || e.model_edge_ids_!=std::array<int,2>{binding.a,binding.b} ||
        e.observed_segment_ids_.size()!=2 || e.observed_segment_ids_[0]<0 || e.observed_segment_ids_[1]<0 ||
        e.observed_segment_ids_[0]==e.observed_segment_ids_[1]) return fail("MISSING_CORNER_EVIDENCE");
    if (!observed::finite(corner) || !observed::finite(e.intersection_) ||
        !std::isfinite(e.error_) || e.error_<0 || !std::isfinite(e.corner_error_px_) || e.corner_error_px_<0)
        return fail("NONFINITE_OR_NEGATIVE_EVIDENCE");
    const cv::Vec4d lines[]{e.line_a_,e.line_b_};
    for (int edge=0;edge<2;++edge) {
        for (double value:lines[edge].val) if (!std::isfinite(value)) return fail("INVALID_FIT_LINE");
        if (!(std::hypot(lines[edge][0],lines[edge][1])>0) ||
            e.original_support_arcs_[edge].size()<size_t(config.min_line_points_)) return fail("SHORT_SUPPORT");
        for (double value:{e.line_mean_residual_px_[edge],e.line_max_residual_px_[edge],
             e.model_mean_residual_px_[edge],e.model_max_residual_px_[edge],e.support_extension_px_[edge]})
            if (!std::isfinite(value) || value<0) return fail("NONFINITE_OR_NEGATIVE_EVIDENCE");
    }
    if (e.original_turn_arc_.size()<2) return fail("MISSING_TURN_ARC");
    return true;
}
}
