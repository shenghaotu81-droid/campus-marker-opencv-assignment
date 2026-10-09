// 完整轮廓上分段并保留原始索引；联合验证两条指定外边及其凸转折连接弧。
// 不允许独立挑两条方向最近的无限线，也不使用预测交点补观测。
#include "corners/corner_edge_fit.hpp"
#include "corners/sandbox_profile.hpp"
#include "corners/sandbox_periodic.hpp"
#include "core/observed_geometry_utils.hpp"
#include "core/corner_budget.hpp"
#include <opencv2/imgproc.hpp>
#include <set>
#include <map>
#include <sstream>
#include <cstdlib>

namespace mark
{
    namespace
    {
        struct Arc
        {
            size_t begin, end;
            size_t source_front=0,source_back=0;
            unsigned position_mask=3;
            std::vector<cv::Point2d> points;
            cv::Vec4d line;
            double mean, maximum;
            double model_mean=0,model_max=0,model_coefficient=0;
            int model_kind=0,model_axis=1;
            double min_x=INFINITY,max_x=-INFINITY,min_y=INFINITY,max_y=-INFINITY;
            unsigned edge_mask = 0;
            std::set<std::pair<double, double>> support;
        };

        // 普通L2拟合；支持点来自连续弧，至少三个去重点且具有预算要求的实际跨度。
        bool fitArc(Arc &arc, const CornerConfig &config, int model_kind)
        {
            sandbox::Scope profile(sandbox::ArcFit);
            for (auto p : arc.points)
                arc.support.emplace(p.x, p.y);
            if (arc.support.size() < static_cast<size_t>(config.min_line_points_))
                return false;
            arc.model_kind=model_kind;
            if(model_kind==1||model_kind==3||model_kind==4){auto f=model_kind==4?sandbox::fitPeriodicLAD(arc.points):model_kind==3?sandbox::fitPeriodicMedian(arc.points):sandbox::fitPeriodic(arc.points);if(!f.valid)return false;arc.line=f.line;arc.model_axis=f.axis;arc.model_coefficient=f.coefficient;}
            else if(model_kind==2){std::vector<cv::Point2d>corrected;corrected.reserve(arc.points.size());arc.model_coefficient=sandbox::frame_shift;for(auto p:arc.points)corrected.emplace_back(p.x-arc.model_coefficient*sandbox::phase(p,1),p.y);cv::fitLine(corrected,arc.line,cv::DIST_L2,0,.01,.01);}
            else cv::fitLine(arc.points, arc.line, cv::DIST_L2, 0, 0.01, 0.01);
            double line_norm = std::hypot(arc.line[0], arc.line[1]);
            if (!(line_norm > 0))
                return false;
            arc.line[0] /= line_norm;
            arc.line[1] /= line_norm;
            cv::Point2d v{arc.line[0], arc.line[1]}, origin{arc.line[2], arc.line[3]};
            double lo = INFINITY, hi = -INFINITY, sum = 0,model_sum=0;
            arc.maximum = 0;
            for (auto p : arc.points)
            {
                arc.min_x=std::min(arc.min_x,p.x);arc.max_x=std::max(arc.max_x,p.x);arc.min_y=std::min(arc.min_y,p.y);arc.max_y=std::max(arc.max_y,p.y);
                double t = (p - origin).dot(v);
                lo = std::min(lo, t);
                hi = std::max(hi, t);
                double d = std::abs(v.x * (p.y - origin.y) - v.y * (p.x - origin.x));
                sum += d;
                double corrected=sandbox::correctedDistance(p,arc.line,arc.model_kind,arc.model_axis,arc.model_coefficient);
                model_sum+=corrected;arc.model_max=std::max(arc.model_max,corrected);
                arc.maximum = std::max(arc.maximum, d);
            }
            arc.mean = sum / arc.points.size();arc.model_mean=model_sum/arc.points.size();
            return hi - lo >= config.observation_budget_->min_support_span_px &&
                   arc.model_mean <= config.max_line_fit_error_;
        }

        // 指定有限模型边的方向与位置共同限制候选；内侧平行边不能仅凭方向获选。
        bool matches(const Arc &arc, const std::array<cv::Point2d, 2> &edge,
                     const CornerObservationBudget &budget, bool fitted_position)
        {
            if (observed::angleDeg({arc.line[0], arc.line[1]}, edge[1] - edge[0]) >
                budget.max_edge_direction_diff_deg)
                return false;
            for (auto p : arc.points)
                if (observed::segmentDistance(fitted_position ? cv::Point2d(arc.line[2],arc.line[3])+cv::Point2d(arc.line[0],arc.line[1])*((p-cv::Point2d(arc.line[2],arc.line[3])).dot(cv::Point2d(arc.line[0],arc.line[1]))) : p, edge[0], edge[1]) >
                    budget.max_edge_position_distance_px)
                    return false;
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

    static EdgePairFitResult fitObservedEdgePairImpl(const std::vector<cv::Point> &contour,
                                          const std::array<std::array<cv::Point2d, 2>, 2> &edges,
                                          const CornerConfig &config, bool all_endpoints, bool fitted_position, int model_kind=0)
    {
        sandbox::Scope profile(sandbox::Fit);
        EdgePairFitResult result;
        validateCornerObservationBudget(config);
        const auto &budget = *config.observation_budget_;
        // 沙盒性能：前缀只是必要条件过滤器；近预算处仍执行原逐段加法。
        std::vector<double> walk_prefix(contour.size()+1,0);
        std::map<std::pair<int,int>,size_t> first_pixel;
        for(size_t k=0;k<contour.size();++k){first_pixel.emplace(std::make_pair(contour[k].x,contour[k].y),k);walk_prefix[k+1]=walk_prefix[k]+cv::norm(contour[(k+1)%contour.size()]-contour[k]);}
        std::vector<unsigned> pixel_mask(contour.size(),3);
        if(!fitted_position)for(size_t k=0;k<contour.size();++k)for(unsigned e=0;e<2;++e)
          if(observed::segmentDistance(contour[k],edges[e][0],edges[e][1])>budget.max_edge_position_distance_px)pixel_mask[k]&=~(1u<<e);
        const double prefix_roundoff=64*std::numeric_limits<double>::epsilon()*contour.size()*std::max(1.,walk_prefix.back());
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
            auto it = std::find(contour.begin(), contour.end(), p);
            if (it == contour.end())
            {
                result.reason = "INVALID_ARC_INDEX";
                return result;
            }
            indices.push_back(static_cast<size_t>(it - contour.begin()));
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
        // 沙盒A：失败后枚举全部真实端点，不删区间中的像素。
        if(all_endpoints){indices.clear();for(size_t k=0;k<contour.size();++k)indices.push_back(k);}
        std::sort(indices.begin(), indices.end());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        std::vector<Arc> arcs;
        std::set<std::vector<std::pair<double,double>>> distinct_support;
        std::map<std::string, size_t> rejected;
        // 简化只用于标记真实轮廓上的转折，支持点仍来自CHAIN_APPROX_NONE连续弧。
        // 栅格圆角会在一条直边末端插入额外简化顶点，原单段实现误当缺边。
        // 实拍栅格边界可能将一条外边拆成多个简化段，不能把相邻段数固定为两段。
        // 枚举当前闭轮廓上的全部非整圈连续区间：P个简化顶点至多P(P-1)候选，
        // 没有任意子集、跨点拼接或误差裁点；所有预算保持不变。
        { sandbox::Scope profile(sandbox::ArcGeneration);
        for (size_t i = 0; i < indices.size(); ++i)
            for (size_t segments = 1; segments < indices.size(); ++segments)
            {
                Arc arc{};
                arc.begin = indices[i];
                arc.end = indices[(i + segments) % indices.size()];
                bool started = false, ended = false, continuous = true;
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
                        if(!fitted_position){arc.position_mask&=pixel_mask[k];if(!arc.position_mask)break;}
                    }
                    else if (started)
                        ended = true;
                    if (k == arc.end)
                        break;
                }
                if(!arc.position_mask){++rejected["direction_or_position_arc"];continue;}
                if (!continuous)
                    ++rejected["discontinuous_arc"];
                else
                {
                    // 沙盒A：只有完整有序支持序列相同才去重，避免回走路径误合并。
                    if(all_endpoints&&!arc.points.empty()){std::vector<std::pair<double,double>> key;for(auto q:arc.points)key.emplace_back(q.x,q.y);if(!distinct_support.insert(key).second)continue;}
                    // 有限模型边的位置检查无需拟合，先排除跨过其他外边的长区间。
                    unsigned position_mask = arc.position_mask;
                    if (!position_mask)
                    {
                        ++rejected["direction_or_position_arc"];
                        continue;
                    }
                    if (!fitArc(arc, config, model_kind))
                    {
                        ++rejected["short_or_residual_arc"];
                        continue;
                    }
                    // 两条指定有限边已在配对前可知；先剔除不属于任一指定边的连续弧，
                    // 避免细碎轮廓将大量无关短边带入二次配对，不改变最终匹配条件。
                    for (unsigned edge = 0; edge < 2; ++edge)
                        if ((position_mask & (1u << edge)) && matches(arc, edges[edge], budget, fitted_position))
                            arc.edge_mask |= 1u << edge;
                    if (arc.edge_mask){
                        arc.source_front=first_pixel.at({cvRound(arc.points.front().x),cvRound(arc.points.front().y)});
                        arc.source_back=first_pixel.at({cvRound(arc.points.back().x),cvRound(arc.points.back().y)});
                        arcs.push_back(std::move(arc));
                    }
                    else
                        ++rejected["direction_or_position_arc"];
                }
            }
        }
        const double winding = cv::contourArea(contour, true);
        { sandbox::Scope profile(sandbox::Pairs);
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
                // 先按有序轮廓真实弧长做保守排除，避免对数百万不邻接弧对做set交集。
                const size_t start_index=a.source_back,stop_index=b.source_front;
                const double connection_estimate=stop_index>=start_index?walk_prefix[stop_index]-walk_prefix[start_index]:walk_prefix.back()-walk_prefix[start_index]+walk_prefix[stop_index];
                if(connection_estimate-prefix_roundoff>budget.max_turn_connection_length_px){++rejected["connection"];continue;}
                bool shared = false;
                if(!(a.max_x<b.min_x||b.max_x<a.min_x||a.max_y<b.min_y||b.max_y<a.min_y))for (auto p : b.points)
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
                size_t k=start_index,stop=stop_index;
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
                // 所有门控均执行后，歧义检查仍无条件覆盖低分候选；只延迟深拷贝。
                const double candidate_error=first.mean+second.mean;
                if(result.evidence){
                    double delta=cv::norm(result.evidence->intersection_-intersection);
                    if(delta*delta>config.semantic_geometry_threshold_){result.evidence.reset();result.reason="AMBIGUOUS_EDGE_PAIR: 外边观测解几何冲突";return result;}
                    if(candidate_error>result.evidence->error_)continue;
                    if(candidate_error==result.evidence->error_){
                        std::array<int,2>new_begin{{int(first.begin),int(second.begin)}},new_end{{int(first.end),int(second.end)}};
                        std::array<int,2>old_begin{{result.evidence->observed_segment_ids_[0],result.evidence->observed_segment_ids_[1]}};
                        if(!(std::tie(new_begin,new_end)<std::tie(old_begin,result.evidence->observed_segment_end_ids_)))continue;
                    }
                }
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
                evidence.sandbox_model_kind_={first.model_kind,second.model_kind};
                evidence.sandbox_model_axis_={first.model_axis,second.model_axis};
                evidence.sandbox_model_coefficient_={first.model_coefficient,second.model_coefficient};
                evidence.sandbox_model_mean_={first.model_mean,second.model_mean};
                evidence.sandbox_model_max_={first.model_max,second.model_max};
                evidence.line_max_residual_px_ = {first.maximum, second.maximum};
                evidence.support_extension_px_ = {ea, eb};
                evidence.corner_error_px_ = corner_error;
                evidence.error_ = first.mean + second.mean;
                result.evidence = std::move(evidence);
            }
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
    // 沙盒路由由环境变量选择，仅供比较；正式实施应使用实例内策略而非全局开关。
    EdgePairFitResult fitObservedEdgePair(const std::vector<cv::Point>&contour,
      const std::array<std::array<cv::Point2d,2>,2>&edges,const CornerConfig&config){
        auto r=fitObservedEdgePairImpl(contour,edges,config,false,false);
        const char*route=std::getenv("MARK_SANDBOX_ROUTE");
        if(!route||std::string(route)=="baseline"||r.evidence)return r;
        r=fitObservedEdgePairImpl(contour,edges,config,false,true);
        if(!r.evidence)r=fitObservedEdgePairImpl(contour,edges,config,true,false);
        int model=sandbox::modelMode();
        if(!r.evidence&&model){r=fitObservedEdgePairImpl(contour,edges,config,false,false,model);
          if(!r.evidence)r=fitObservedEdgePairImpl(contour,edges,config,false,true,model);
          if(!r.evidence)r=fitObservedEdgePairImpl(contour,edges,config,true,false,model);
          if(!r.evidence&&model==3){r=fitObservedEdgePairImpl(contour,edges,config,false,false,4);
            if(!r.evidence)r=fitObservedEdgePairImpl(contour,edges,config,false,true,4);
            if(!r.evidence)r=fitObservedEdgePairImpl(contour,edges,config,true,false,4);}
        }
        return r;
    }

}
