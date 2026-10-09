#include "geometry/geometry_l_topology.hpp"
#include "geometry/assignment_match_metrics.hpp"
#include <set>
#include <algorithm>

namespace mark
{
    namespace
    {
        // 固定六边、五凸一凹，按绕向归一，拒绝共线点；不拿顶点数量本身作L身份。
        bool explicitL(const std::vector<cv::Point2d> &p, size_t &concave)
        {
            if (p.size() != 6)
                return false;
            int count = 0;
            for (size_t i = 0; i < 6; ++i)
            {
                double turn = normalizedTurn(p, i);
                if (!std::isfinite(turn) || turn == 0)
                    return false;
                if (turn < 0)
                {
                    concave = i;
                    ++count;
                }
            }
            if (count != 1)
                return false;
            auto at = [&](int offset)
            {
                return p[(concave + offset + 6) % 6];
            };
            auto c = at(0), u = at(-1) - c, v = at(1) - c;
            double uu = u.dot(u), vv = v.dot(v);
            if (!(uu > 0 && vv > 0))
                return false;
            // 真L两条内臂长于相应笔画宽；M虽也是六边一凹，其内臂短于笔画，不能冒充L。
            // 比值沿同一平行边族，等比/非等比非退化仿射均保持，不新添角度/面积预算。
            double a = -(at(2) - at(1)).dot(u) / uu, b = -(at(-2) - at(-1)).dot(v) / vv;
            if (!(a > 0 && a < 1 && b > 0 && b < 1))
                return false;
            // 观察层只判实测拓扑/长臂，不能另加epsilon闭合残差冒充原5px父验证。
            // 各候选仍先通过完整原轮廓的简化距离检查，几何一致性由原validator判定。
            return true;
        }
    }

    // 复用原结构规则，防止内部伪候选借 L 类别进入拟合；不添加拟合／原图门限。
    bool is_valid_l_topology_candidate(const LTopologyCandidate &candidate)
    {
        if (candidate.polygon_.size() != 6 || !std::isfinite(candidate.simplification_epsilon_) ||
            candidate.simplification_epsilon_ <= 0 ||
            candidate.concave_vertex_indices_.size() != 1 ||
            candidate.concave_vertex_indices_[0] >= 6)
            return false;
        std::vector<cv::Point2d> polygon;
        for (auto p : candidate.polygon_)
        {
            if (!std::isfinite(p.x) || !std::isfinite(p.y))
                return false;
            polygon.emplace_back(p);
        }
        size_t concave = 0;
        return explicitL(polygon, concave) && concave == candidate.concave_vertex_indices_[0];
    }

    // 固定六个epsilon比例，至多两额外顶点的实测子序列；不从H/video逐例调参数。
    std::vector<LTopologyCandidate> observeLTopologies(const WhiteComponent &component,
                                                       const GeometryConfig &config)
    {
        std::vector<LTopologyCandidate> result;
        if (component.contour_.size() < 6 || !std::isfinite(config.approximation_epsilon_) ||
            config.approximation_epsilon_ <= 0)
            return result;
        std::set<std::vector<std::pair<float, float>>> seen;
        // 先保留原epsilon候选的顺序，再补固定尺度；max=2epsilon是明确观察规则，不改变5px验证门限。
        for (double fraction : {1., .5, .75, 1.25, 1.5, 2.})
        {
            double epsilon = config.approximation_epsilon_ * fraction;
            std::vector<cv::Point> simple;
            cv::approxPolyDP(component.contour_, simple, epsilon, true);
            for (const auto &polygon :
                 boundedTopologyPolygons(component.contour_, simple, 6, epsilon))
            {
                size_t concave = 0;
                if (!explicitL(polygon, concave))
                    continue;
                std::vector<std::pair<float, float>> key;
                for (auto p : polygon)
                    key.emplace_back(float(p.x), float(p.y));
                if (!seen.insert(key).second)
                    continue;
                LTopologyCandidate candidate;
                candidate.simplification_epsilon_ = epsilon;
                candidate.concave_vertex_indices_.push_back(concave);
                for (auto p : polygon)
                    candidate.polygon_.emplace_back(p);
                result.push_back(std::move(candidate));
            }
        }
        return result;
    }
}
