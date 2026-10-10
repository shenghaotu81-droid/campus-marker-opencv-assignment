// 本轮先独立验收的完整枚举器保留为 reference；仅测试链接，绝非复制历史沙盒。
// 完整轮廓上分段并保留原始索引；联合验证两条指定外边及其凸转折连接弧。
// 不允许独立挑两条方向最近的无限线，也不使用预测交点补观测。
#include "recovery112_reference.hpp"
#include "corners/periodic_line_fit.hpp"
#include "core/observed_geometry_utils.hpp"
#include "core/corner_budget.hpp"
#include <opencv2/imgproc.hpp>
#include <set>
#include <map>
#include <sstream>
#include <unordered_map>
#include <numeric>

namespace mark::recovery112_reference
{
    namespace
    {
        struct Arc
        {
            size_t begin, end;
            std::vector<cv::Point2d> points;
            cv::Vec4d line;
            double mean, maximum;
            unsigned edge_mask = 0;
            std::set<std::pair<double, double>> support;
            // 原共享像素检查无论空间上是否相交都查询集合；残差遍历顺便保存精确 AABB。
            cv::Point2d minimum{INFINITY, INFINITY}, maximum_point{-INFINITY, -INFINITY};
            // 旧 mean/maximum 保留 raw 含义；模型误差单独用于新增周期质量门控。
            LineFitModel model{LineFitModel::Legacy};
            int axis{0};
            double coefficient{0}, model_mean{INFINITY}, model_maximum{INFINITY};
        };

        // CHAIN_APPROX_NONE 可以回走；坐标键只缓存 find 的首次索引，不合并有序支持路径。
        uint64_t pixelKey(cv::Point point)
        {
            return (static_cast<uint64_t>(static_cast<uint32_t>(point.x)) << 32) |
                   static_cast<uint32_t>(point.y);
        }

        // 普通L2拟合；支持点来自连续弧，至少三个去重点且具有预算要求的实际跨度。
        bool fitArc(Arc &arc, const CornerConfig &config, LineFitModel model)
        {
            for (auto p : arc.points)
                arc.support.emplace(p.x, p.y);
            if (arc.support.size() < static_cast<size_t>(config.min_line_points_))
                return false;
            arc.model = model;
            if (model == LineFitModel::Legacy)
                cv::fitLine(arc.points, arc.line, cv::DIST_L2, 0, 0.01, 0.01);
            else
            {
                // 原单线不能表达两行结构；物理线只取趋势，c 只解释扫描相位，不改支持。
                const auto periodic = fitPeriodicLine(arc.points, model);
                if (!periodic)
                    return false;
                arc.line = periodic->line;
                arc.axis = periodic->axis;
                arc.coefficient = periodic->coefficient;
            }
            double line_norm = std::hypot(arc.line[0], arc.line[1]);
            if (!(line_norm > 0))
                return false;
            arc.line[0] /= line_norm;
            arc.line[1] /= line_norm;
            cv::Point2d v{arc.line[0], arc.line[1]}, origin{arc.line[2], arc.line[3]};
            double lo = INFINITY, hi = -INFINITY, sum = 0, model_sum = 0;
            arc.maximum = 0;
            arc.model_maximum = 0;
            for (auto p : arc.points)
            {
                arc.minimum.x = std::min(arc.minimum.x, p.x);
                arc.minimum.y = std::min(arc.minimum.y, p.y);
                arc.maximum_point.x = std::max(arc.maximum_point.x, p.x);
                arc.maximum_point.y = std::max(arc.maximum_point.y, p.y);
                double t = (p - origin).dot(v);
                lo = std::min(lo, t);
                hi = std::max(hi, t);
                double d = std::abs(v.x * (p.y - origin.y) - v.y * (p.x - origin.x));
                sum += d;
                arc.maximum = std::max(arc.maximum, d);
                const double model_distance =
                    model == LineFitModel::Legacy
                        ? d
                        : periodicLineDistance(p, arc.line, arc.axis, arc.coefficient);
                model_sum += model_distance;
                arc.model_maximum = std::max(arc.model_maximum, model_distance);
            }
            arc.mean = sum / arc.points.size();
            arc.model_mean = model_sum / arc.points.size();
            return hi - lo >= config.observation_budget_->min_support_span_px &&
                   arc.model_mean <= config.max_line_fit_error_;
        }

        // 指定有限模型边的方向与位置共同限制候选；内侧平行边不能仅凭方向获选。
        bool matches(const Arc &arc, const std::array<cv::Point2d, 2> &edge,
                     const CornerObservationBudget &budget, bool posterior)
        {
            if (observed::angleDeg({arc.line[0], arc.line[1]}, edge[1] - edge[0]) >
                budget.max_edge_direction_diff_deg)
                return false;
            for (auto p : arc.points)
            {
                // 原像素粗糙度不等于物理线位置；后验模式投影全部实测点，仍检查指定有限边。
                if (posterior)
                {
                    const cv::Point2d origin{arc.line[2], arc.line[3]},
                        direction{arc.line[0], arc.line[1]};
                    p = origin + direction * (p - origin).dot(direction);
                }
                if (observed::segmentDistance(p, edge[0], edge[1]) >
                    budget.max_edge_position_distance_px)
                    return false;
            }
            return true;
        }

        // 交点在线段内为0，外部只记录到近端点的延伸，不把整条长边算误差。
        double extension(cv::Point2d p, const Arc &arc)
        {
            cv::Point2d v{arc.line[0], arc.line[1]};
            double lo = INFINITY, hi = -INFINITY;
            for (auto q : arc.points)
            {
                double t = (q - p).dot(v);
                lo = std::min(lo, t);
                hi = std::max(hi, t);
            }
            return lo > 0 ? lo : hi < 0 ? -hi : 0;
        }
    }

    // 原候选只覆盖简化端点；补查仍枚举真实轮廓连续弧，分别修复位置检查时机和端点覆盖。
    static EdgePairFitResult fitStage(const std::vector<cv::Point> &contour,
                                      const std::array<std::array<cv::Point2d, 2>, 2> &edges,
                                      const CornerConfig &config, bool all_endpoints,
                                      bool posterior, LineFitModel model, EdgePairFitTrace *trace,
                                      size_t stage)
    {
        EdgePairFitResult result;
        validateCornerObservationBudget(config);
        const auto &budget = *config.observation_budget_;
        // 原位置距离在每条重叠弧内重复算；逐像素预计算同一有限边谓词，并构建循环长度表。
        std::vector<unsigned> pixel_masks(contour.size(), 3);
        std::unordered_map<uint64_t, size_t> first_indices;
        std::vector<double> lengths(contour.size() + 1, 0);
        for (size_t k = 0; k < contour.size(); ++k)
        {
            first_indices.emplace(pixelKey(contour[k]), k);
            for (unsigned edge = 0; edge < 2; ++edge)
                if (observed::segmentDistance(contour[k], edges[edge][0], edges[edge][1]) >
                    budget.max_edge_position_distance_px)
                    pixel_masks[k] &= ~(1u << edge);
            lengths[k + 1] = lengths[k] + cv::norm(contour[(k + 1) % contour.size()] - contour[k]);
        }
        // 前缀差仅用于明显超预算的必要条件；边界附近仍按原逐段顺序求和，门限不变。
        const double length_roundoff = 64 * std::numeric_limits<double>::epsilon() *
                                       contour.size() * std::max(1.0, lengths.back());
        std::vector<cv::Point> polygon;
        cv::approxPolyDP(contour, polygon, config.approximation_epsilon_, true);
        if (polygon.size() < 3)
        {
            result.reason = "SHORT_SUPPORT: 原图轮廓不足";
            return result;
        }
        std::vector<size_t> indices;
        for (auto p : polygon)
        {
            auto it = first_indices.find(pixelKey(p));
            if (it == first_indices.end())
            {
                result.reason = "INVALID_ARC_INDEX";
                return result;
            }
            indices.push_back(it->second);
        }
        // 简化顶点落在一像素回走尖刺上时，固定顶点端点也会把有效直边卡在残差上。
        // 在既有转折剔除距离内补充真实轮廓端点；距离同时按沿弧步长限制，
        // 不跳去远处平行边，不移造坐标，不删除区间中的任何观测像素。
        const auto simplified_indices = indices;
        for (auto anchor : simplified_indices)
            for (int sign : {-1, 1})
            {
                size_t previous = anchor;
                double walk = 0;
                for (size_t step = 1; step < contour.size(); ++step)
                {
                    size_t current = sign > 0 ? (anchor + step) % contour.size()
                                              : (anchor + contour.size() - step) % contour.size();
                    walk += cv::norm(contour[current] - contour[previous]);
                    if (walk > budget.turn_trim_distance_px)
                        break;
                    indices.push_back(current);
                    previous = current;
                }
            }
        std::sort(indices.begin(), indices.end());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        if (all_endpoints)
        {
            indices.resize(contour.size());
            std::iota(indices.begin(), indices.end(), 0);
        }
        std::vector<Arc> arcs;
        // 全端点的不同区间可能回走到同一有序像素序列；必须按全序列去重，不能只看端点。
        std::set<std::vector<std::pair<double, double>>> sequences;
        std::map<std::string, size_t> rejected;
        // 简化只用于标记真实轮廓上的转折，支持点仍来自CHAIN_APPROX_NONE连续弧。
        // 栅格圆角会在一条直边末端插入额外简化顶点，原单段实现误当缺边。
        // 实拍栅格边界可能将一条外边拆成多个简化段，不能把相邻段数固定为两段。
        // 枚举当前闭轮廓上的全部非整圈连续区间：P个简化顶点至多P(P-1)候选，
        // 没有任意子集、跨点拼接或误差裁点；所有预算保持不变。
        for (size_t i = 0; i < indices.size(); ++i)
            for (size_t segments = 1; segments < indices.size(); ++segments)
            {
                Arc arc{};
                arc.begin = indices[i];
                arc.end = indices[(i + segments) % indices.size()];
                bool started = false, ended = false, continuous = true;
                unsigned position_mask = 3;
                for (size_t k = arc.begin;; k = (k + 1) % contour.size())
                {
                    cv::Point2d p = contour[k];
                    if (cv::norm(p - cv::Point2d(contour[arc.begin])) >=
                            budget.turn_trim_distance_px &&
                        cv::norm(p - cv::Point2d(contour[arc.end])) >= budget.turn_trim_distance_px)
                    {
                        if (ended)
                            continuous = false;
                        started = true;
                        arc.points.push_back(p);
                        // AND 为零后任何后续像素都不能使位置谓词恢复，故可精确提前拒绝。
                        if (!posterior)
                            position_mask &= pixel_masks[k];
                        if (!position_mask)
                            break;
                    }
                    else if (started)
                        ended = true;
                    if (k == arc.end)
                        break;
                }
                if (!continuous)
                    ++rejected["discontinuous_arc"];
                else
                {
                    // 有限模型边的位置检查无需拟合，先排除跨过其他外边的长区间。
                    if (!position_mask)
                    {
                        ++rejected["direction_or_position_arc"];
                        continue;
                    }
                    if (all_endpoints)
                    {
                        std::vector<std::pair<double, double>> sequence;
                        for (auto p : arc.points)
                            sequence.emplace_back(p.x, p.y);
                        if (!sequences.insert(std::move(sequence)).second)
                            continue;
                    }
                    if (!fitArc(arc, config, model))
                    {
                        ++rejected["short_or_residual_arc"];
                        continue;
                    }
                    // 两条指定有限边已在配对前可知；先剔除不属于任一指定边的连续弧，
                    // 避免细碎轮廓将大量无关短边带入二次配对，不改变最终匹配条件。
                    for (unsigned edge = 0; edge < 2; ++edge)
                        if ((position_mask & (1u << edge)) &&
                            matches(arc, edges[edge], budget, posterior))
                            arc.edge_mask |= 1u << edge;
                    if (arc.edge_mask)
                    {
                        // 逐别名记录全部合格弧，包含完整有序支持，供精确索引实现逐项对账。
                        if (trace)
                            trace->arcs.push_back({stage, arc.begin, arc.end, arc.edge_mask,
                                                   arc.line, arc.points, arc.mean, arc.maximum,
                                                   arc.model_mean, arc.model_maximum, arc.model,
                                                   arc.axis, arc.coefficient});
                        arcs.push_back(std::move(arc));
                    }
                    else
                        ++rejected["direction_or_position_arc"];
                }
            }
        const double winding = cv::contourArea(contour, true);
        for (size_t i = 0; i < arcs.size(); ++i)
            for (size_t j = 0; j < arcs.size(); ++j)
            {
                if (i == j)
                    continue;
                const auto &a = arcs[i];
                const auto &b = arcs[j];
                // 缓存同一连续弧的匹配身份和支持集合，避免每一对重复拟合/建集合。
                bool direct = (a.edge_mask & 1) && (b.edge_mask & 2);
                bool reverse = (a.edge_mask & 2) && (b.edge_mask & 1);
                if (!direct && !reverse)
                    continue;
                bool shared = false;
                // AABB 不相交严格证明无共享像素；相交时仍逐点执行原集合判据。
                if (!(a.maximum_point.x < b.minimum.x || b.maximum_point.x < a.minimum.x ||
                      a.maximum_point.y < b.minimum.y || b.maximum_point.y < a.minimum.y))
                    for (auto p : b.points)
                        if (a.support.count({p.x, p.y}))
                        {
                            shared = true;
                            break;
                        }
                if (shared)
                {
                    ++rejected["reused_support"];
                    continue;
                }
                // 相邻支持弧之间允许圆角连接，连接长度受独立预算约束。
                std::vector<cv::Point2d> connector;
                size_t k = first_indices.at(pixelKey(cv::Point(a.points.back()))),
                       stop = first_indices.at(pixelKey(cv::Point(b.points.front())));
                const double prefix_length = stop >= k
                                                 ? lengths[stop] - lengths[k]
                                                 : lengths.back() - lengths[k] + lengths[stop];
                if (prefix_length > budget.max_turn_connection_length_px + length_roundoff)
                {
                    ++rejected["connection"];
                    continue;
                }
                double length = 0;
                for (;;)
                {
                    auto p = cv::Point2d(contour[k]);
                    if (!connector.empty())
                        length += cv::norm(p - connector.back());
                    connector.push_back(p);
                    if (k == stop || length > budget.max_turn_connection_length_px)
                        break;
                    k = (k + 1) % contour.size();
                }
                if (k != stop || length > budget.max_turn_connection_length_px)
                {
                    ++rejected["connection"];
                    continue;
                }
                cv::Point2d da = a.points.back() - a.points.front(),
                            db = b.points.back() - b.points.front();
                if ((da.x * db.y - da.y * db.x) * winding <= 0)
                {
                    ++rejected["nonconvex"];
                    continue;
                }
                const Arc &first = direct ? a : b;
                const Arc &second = direct ? b : a;
                cv::Point2d v{first.line[0], first.line[1]}, w{second.line[0], second.line[1]};
                double theta = observed::angleDeg(v, w);
                if (!std::isfinite(theta) || theta < config.min_intersection_angle_deg_)
                {
                    ++rejected["angle"];
                    continue;
                }
                double cross = v.x * w.y - v.y * w.x;
                cv::Point2d p{first.line[2], first.line[3]}, q{second.line[2], second.line[3]},
                    d = q - p;
                cv::Point2d intersection = p + v * ((d.x * w.y - d.y * w.x) / cross);
                if (!observed::finite(intersection))
                    continue;
                double corner_error = observed::boundaryDistance(intersection, connector, false);
                double ea = extension(intersection, first), eb = extension(intersection, second);
                if (corner_error > config.max_corner_error_ ||
                    ea > budget.max_support_extension_px || eb > budget.max_support_extension_px)
                {
                    ++rejected["corner_or_extension"];
                    continue;
                }
                if (trace)
                    trace->pairs.push_back({stage,
                                            {first.begin, first.end, second.begin, second.end},
                                            intersection,
                                            connector,
                                            corner_error,
                                            ea,
                                            eb});
                // 即使评分较差也先做歧义检查；只有成为最佳候选时才复制完整取证序列。
                if (result.evidence &&
                    cv::norm(result.evidence->intersection_ - intersection) *
                            cv::norm(result.evidence->intersection_ - intersection) >
                        config.semantic_geometry_threshold_)
                {
                    result.evidence.reset();
                    result.reason = "AMBIGUOUS_EDGE_PAIR: 外边观测解几何冲突";
                    return result;
                }
                const double error = first.mean + second.mean;
                const std::vector<int> ids{static_cast<int>(first.begin),
                                           static_cast<int>(second.begin)};
                const std::array<int, 2> ends{
                    {static_cast<int>(first.end), static_cast<int>(second.end)}};
                if (result.evidence &&
                    !(error < result.evidence->error_ ||
                      (error == result.evidence->error_ &&
                       std::tie(ids, ends) < std::tie(result.evidence->observed_segment_ids_,
                                                      result.evidence->observed_segment_end_ids_))))
                    continue;
                CornerEvidence evidence{};
                evidence.original_observation_ = true;
                evidence.truncated_ = false;
                evidence.original_support_arcs_ = {first.points, second.points};
                evidence.original_turn_arc_ = connector;
                evidence.observed_segment_ids_ = {static_cast<int>(first.begin),
                                                  static_cast<int>(second.begin)};
                evidence.observed_segment_end_ids_ = {static_cast<int>(first.end),
                                                      static_cast<int>(second.end)};
                evidence.edge_segment_a_ = {first.points.front(), first.points.back()};
                evidence.edge_segment_b_ = {second.points.front(), second.points.back()};
                evidence.line_a_ = first.line;
                evidence.line_b_ = second.line;
                evidence.intersection_ = intersection;
                evidence.line_mean_residual_px_ = {first.mean, second.mean};
                evidence.line_max_residual_px_ = {first.maximum, second.maximum};
                // 证据同时记录估计器和校正误差；error_ 仍用原 raw 均值之和排序。
                evidence.line_model_kinds_ = {first.model, second.model};
                evidence.periodic_axes_ = {first.axis, second.axis};
                evidence.periodic_coefficients_ = {first.coefficient, second.coefficient};
                evidence.model_mean_residual_px_ = {first.model_mean, second.model_mean};
                evidence.model_max_residual_px_ = {first.model_maximum, second.model_maximum};
                evidence.support_extension_px_ = {ea, eb};
                evidence.corner_error_px_ = corner_error;
                evidence.error_ = first.mean + second.mean;
                result.evidence = std::move(evidence);
            }
        if (!result.evidence)
        {
            std::ostringstream reason;
            reason << "NO_VALID_ADJACENT_EDGES: polygon=" << polygon.size()
                   << ",eligible_arcs=" << arcs.size();
            for (const auto &count : rejected)
                reason << ',' << count.first << '=' << count.second;
            result.reason = reason.str();
        }
        return result;
    }

    namespace
    {
        // 路由和输入绑定在实例内；失败补查不会改写原成功结果，也不跨帧存状态。
        class EdgePairFitter
        {
          public:
            EdgePairFitter(const std::vector<cv::Point> &contour,
                           const std::array<std::array<cv::Point2d, 2>, 2> &edges,
                           const CornerConfig &config, EdgePairFitStrategy strategy,
                           EdgePairFitTrace *trace)
                : contour_(contour), edges_(edges), config_(config), strategy_(strategy),
                  trace_(trace)
            {
            }

            EdgePairFitResult run()
            {
                auto original = stage(false, false, LineFitModel::Legacy);
                if (original.evidence || strategy_ == EdgePairFitStrategy::OriginalOnly)
                    return original;
                // 快速路线只改变失败补查优先级，reference 仍完整枚举每个阶段。
                if (strategy_ == EdgePairFitStrategy::FastRecovery)
                    for (bool posterior : {false, true})
                        for (auto model :
                             {LineFitModel::PeriodicPhaseMedian, LineFitModel::PeriodicZeroSlope})
                        {
                            auto fast = stage(false, posterior, model);
                            if (fast.evidence)
                                return fast;
                        }
                // B：原端点的实测支持投影到物理线，再用原 9.0 有限段位置门限。
                auto posterior = stage(false, true, LineFitModel::Legacy);
                if (posterior.evidence)
                    return posterior;
                // A：保持原位置模式，覆盖全部真实端点；不是按残差删除区间中的像素。
                auto exhaustive = stage(true, false, LineFitModel::Legacy);
                if (exhaustive.evidence || strategy_ == EdgePairFitStrategy::LinearRecovery)
                    return exhaustive;
                // C1：先固定 OLS 斜率的两相位中位数；仍失败才精化同一 a/b/c 的 L1 目标。
                for (auto model : {LineFitModel::PeriodicPhaseMedian, LineFitModel::PeriodicL1})
                    for (auto mode :
                         {std::pair<bool, bool>{false, false}, {false, true}, {true, false}})
                    {
                        exhaustive = stage(mode.first, mode.second, model);
                        if (exhaustive.evidence)
                            return exhaustive;
                    }
                return exhaustive;
            }

          private:
            EdgePairFitResult stage(bool all, bool posterior, LineFitModel model)
            {
                return fitStage(contour_, edges_, config_, all, posterior, model, trace_, stage_++);
            }

            EdgePairFitTrace *trace_;
            size_t stage_{0};
            const std::vector<cv::Point> &contour_;
            const std::array<std::array<cv::Point2d, 2>, 2> &edges_;
            const CornerConfig &config_;
            const EdgePairFitStrategy strategy_;
        };
    }

    EdgePairFitResult fitObservedEdgePair(const std::vector<cv::Point> &contour,
                                          const std::array<std::array<cv::Point2d, 2>, 2> &edges,
                                          const CornerConfig &config, EdgePairFitStrategy strategy,
                                          EdgePairFitTrace *trace)
    {
        EdgePairFitter fitter(contour, edges, config, strategy, trace);
        return fitter.run();
    }
}
