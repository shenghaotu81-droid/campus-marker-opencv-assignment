// 完整轮廓上的连续支持搜索；索引只复用计算，别名顺序、所有质量门控和歧义检查保留。
#include "corners/corner_edge_fit.hpp"
#include "corners/periodic_line_fit.hpp"
#include "core/corner_budget.hpp"
#include "core/observed_geometry_utils.hpp"
#include <opencv2/imgproc.hpp>
#include <numeric>
#include <set>
#include <sstream>
#include <unordered_map>

namespace mark
{
    namespace
    {
        using ModelEdges = std::array<std::array<cv::Point2d, 2>, 2>;

        uint64_t pixelKey(cv::Point point)
        {
            // 坐标只用于 legacy 首次 find 和精确像素身份，不作为连续弧去重键。
            return (static_cast<uint64_t>(static_cast<uint32_t>(point.x)) << 32) |
                   static_cast<uint32_t>(point.y);
        }

        enum class Rejection : size_t
        {
            Angle,
            Connection,
            CornerOrExtension,
            DirectionOrPosition,
            Discontinuous,
            Nonconvex,
            ReusedSupport,
            ShortOrResidual,
            Silent,
            Passed
        };

        // 原热循环用字符串 map 计数；固定槽消除构造/树查询，输出仍按原字典序。
        constexpr std::array<const char *, 8> rejection_names{
            {"angle", "connection", "corner_or_extension", "direction_or_position_arc",
             "discontinuous_arc", "nonconvex", "reused_support", "short_or_residual_arc"}};

        struct SupportRange
        {
            size_t begin, end, first{0}, last{0};
            bool continuous{true}, empty{true};
            unsigned position_mask{3};
        };

        struct SupportRecord
        {
            size_t id{0}, first{0}, last{0}, legacy_front{0}, legacy_back{0};
            std::vector<cv::Point2d> points;
            bool unique_ready{false}, enough_unique{false};
            // 原唯一点数检查只需证明下限；共享像素全集确实需要时才构建，并且不再修改。
            std::optional<std::set<std::pair<double, double>>> membership;
        };

        struct FittedSupport
        {
            SupportRecord *support{nullptr};
            bool valid{false};
            cv::Vec4d line{};
            LineFitModel model{LineFitModel::Legacy};
            int axis{0};
            double coefficient{0}, mean{INFINITY}, maximum{INFINITY};
            double model_mean{INFINITY}, model_maximum{INFINITY};
            cv::Point2d minimum{INFINITY, INFINITY}, maximum_point{-INFINITY, -INFINITY};
            // 同一支持的原位置/后验位置身份固定，不因端点别名或当前 best 改变。
            std::array<std::optional<unsigned>, 2> matches;
        };

        struct ArcAlias
        {
            size_t begin, end;
            FittedSupport *fit;
            unsigned edge_mask;
        };

        struct PairKey
        {
            size_t first, second;

            bool operator==(const PairKey &other) const
            {
                return first == other.first && second == other.second;
            }
        };

        struct PairHash
        {
            size_t operator()(const PairKey &key) const
            {
                return std::hash<size_t>{}(key.first) ^
                       (std::hash<size_t>{}(key.second) + 0x9e3779b9 + (key.first << 6) +
                        (key.first >> 2));
            }
        };

        struct PairGeometry
        {
            Rejection rejection{Rejection::Silent};
            cv::Point2d intersection{};
            std::vector<cv::Point2d> connector;
            double corner_error{INFINITY}, first_extension{INFINITY}, second_extension{INFINITY};
        };

        struct SequenceLess
        {
            bool operator()(const std::vector<cv::Point2d> *a,
                            const std::vector<cv::Point2d> *b) const
            {
                // 回走可使不同 raw range 产生同一全序列；比较全部有序点，严禁只比较端点。
                return std::lexicographical_compare(a->begin(), a->end(), b->begin(), b->end(),
                                                    [](cv::Point2d p, cv::Point2d q)
                                                    {
                                                        return std::tie(p.x, p.y) <
                                                               std::tie(q.x, q.y);
                                                    });
            }
        };

        // 单次 fit 的 scratch：所有输入只读，缓存不跨帧、组件、模型边或预算复用。
        class EdgePairFitter
        {
          public:
            EdgePairFitter(const std::vector<cv::Point> &contour, const ModelEdges &edges,
                           const CornerConfig &config, EdgePairFitStrategy strategy,
                           EdgePairFitTrace *trace)
                : contour_(contour), edges_(edges), config_(config),
                  budget_(*config.observation_budget_), strategy_(strategy), trace_(trace),
                  exclusions_(contour.size())
            {
                prepareContour();
            }

            EdgePairFitResult run();

          private:
            void prepareContour()
            {
                const size_t count = contour_.size();
                lengths_.assign(count + 1, 0);
                for (auto &prefix : bad_positions_)
                    prefix.assign(count + 1, 0);
                first_occurrences_.resize(count);
                std::unordered_map<uint64_t, size_t> first;
                for (size_t k = 0; k < count; ++k)
                {
                    first_occurrences_[k] = first.emplace(pixelKey(contour_[k]), k).first->second;
                    lengths_[k + 1] =
                        lengths_[k] + cv::norm(contour_[(k + 1) % count] - contour_[k]);
                    for (size_t edge = 0; edge < 2; ++edge)
                        bad_positions_[edge][k + 1] =
                            bad_positions_[edge][k] +
                            (observed::segmentDistance(contour_[k], edges_[edge][0],
                                                       edges_[edge][1]) >
                             budget_.max_edge_position_distance_px);
                }
                // 前缀差有舍入误差；只拒绝明显超预算者，近边界仍走原逐段局部求和。
                length_roundoff_ = 64 * std::numeric_limits<double>::epsilon() * count *
                                   std::max(1.0, lengths_.back());
                cv::approxPolyDP(contour_, polygon_, config_.approximation_epsilon_, true);
                for (auto point : polygon_)
                {
                    const auto found = first.find(pixelKey(point));
                    if (found == first.end())
                        throw std::logic_error("INVALID_ARC_INDEX");
                    original_indices_.push_back(found->second);
                }
                const auto anchors = original_indices_;
                // 原候选端点的沿弧补充邻域及枚举顺序保持，不能用欧氏近邻替代这一步。
                for (auto anchor : anchors)
                    for (int sign : {-1, 1})
                    {
                        size_t previous = anchor;
                        double walk = 0;
                        for (size_t step = 1; step < count; ++step)
                        {
                            const size_t current = sign > 0 ? (anchor + step) % count
                                                            : (anchor + count - step) % count;
                            walk += cv::norm(contour_[current] - contour_[previous]);
                            if (walk > budget_.turn_trim_distance_px)
                                break;
                            original_indices_.push_back(current);
                            previous = current;
                        }
                    }
                std::sort(original_indices_.begin(), original_indices_.end());
                original_indices_.erase(
                    std::unique(original_indices_.begin(), original_indices_.end()),
                    original_indices_.end());
                winding_ = cv::contourArea(contour_, true);
            }

            const std::vector<size_t> &excludedAt(size_t endpoint)
            {
                auto &cached = exclusions_[endpoint];
                if (!cached)
                {
                    cached.emplace();
                    // 原修剪按完整轮廓的 cv::norm < trim；回走到远处索引也必须排除。
                    for (size_t k = 0; k < contour_.size(); ++k)
                        if (cv::norm(cv::Point2d(contour_[k]) - cv::Point2d(contour_[endpoint])) <
                            budget_.turn_trim_distance_px)
                            cached->push_back(k);
                }
                return *cached;
            }

            SupportRange describe(size_t begin, size_t end, std::vector<size_t> &excluded)
            {
                SupportRange range{begin, end};
                const size_t count = contour_.size(), extent = (end + count - begin) % count;
                // 每个区间原来都重新分配短向量；构造描述时复用局部 scratch，谓词和顺序不变。
                excluded.clear();
                for (size_t endpoint : {begin, end})
                    for (auto k : excludedAt(endpoint))
                    {
                        const size_t offset = (k + count - begin) % count;
                        if (offset <= extent)
                            excluded.push_back(offset);
                    }
                std::sort(excluded.begin(), excluded.end());
                excluded.erase(std::unique(excluded.begin(), excluded.end()), excluded.end());
                size_t head = 0;
                for (auto offset : excluded)
                    if (offset == head)
                        ++head;
                    else
                        break;
                if (head > extent)
                    return range;
                ptrdiff_t tail = static_cast<ptrdiff_t>(extent);
                for (auto it = excluded.rbegin(); it != excluded.rend(); ++it)
                    if (static_cast<ptrdiff_t>(*it) == tail)
                        --tail;
                    else
                        break;
                if (tail < static_cast<ptrdiff_t>(head))
                    return range;
                // 原循环在开始保留后再次遇剔除点再恢复即判不连续；中间坏点仍严格拒绝。
                const auto inside = std::lower_bound(excluded.begin(), excluded.end(), head);
                range.continuous = inside == excluded.end() || *inside > static_cast<size_t>(tail);
                range.empty = false;
                range.first = (begin + head) % count;
                range.last = (begin + static_cast<size_t>(tail)) % count;
                for (size_t edge = 0; edge < 2; ++edge)
                {
                    const auto &bad = bad_positions_[edge];
                    const size_t bad_count =
                        range.last >= range.first
                            ? bad[range.last + 1] - bad[range.first]
                            : bad.back() - bad[range.first] + bad[range.last + 1];
                    if (bad_count)
                        range.position_mask &= ~(1u << edge);
                }
                return range;
            }

            const std::vector<SupportRange> &descriptors(bool all)
            {
                auto &cached = descriptors_[all];
                if (!cached)
                {
                    cached.emplace();
                    auto indices = original_indices_;
                    if (all)
                    {
                        indices.resize(contour_.size());
                        std::iota(indices.begin(), indices.end(), 0);
                    }
                    // 支持描述按旧 i/segments 顺序保存；模型/位置阶段复用，不丢原端点别名。
                    cached->reserve(indices.size() * (indices.empty() ? 0 : indices.size() - 1));
                    std::vector<size_t> excluded;
                    for (size_t i = 0; i < indices.size(); ++i)
                        for (size_t segments = 1; segments < indices.size(); ++segments)
                            cached->push_back(describe(
                                indices[i], indices[(i + segments) % indices.size()], excluded));
                }
                return *cached;
            }

            SupportRecord &supportFor(const SupportRange &range)
            {
                const size_t id = range.first * contour_.size() + range.last;
                auto entry = supports_.try_emplace(id);
                auto &support = entry.first->second;
                if (entry.second)
                {
                    support.id = id;
                    support.first = range.first;
                    support.last = range.last;
                    support.legacy_front = first_occurrences_[range.first];
                    support.legacy_back = first_occurrences_[range.last];
                    support.points.reserve(
                        (range.last + contour_.size() - range.first) % contour_.size() + 1);
                    // 仅需要拟合/序列去重时才展开原始支持；包含内部所有回走与重复像素。
                    for (size_t k = range.first;; k = (k + 1) % contour_.size())
                    {
                        support.points.emplace_back(contour_[k]);
                        if (k == range.last)
                            break;
                    }
                }
                return support;
            }

            FittedSupport &fitFor(SupportRecord &support, LineFitModel model);
            unsigned matches(FittedSupport &fit, unsigned position_mask, bool posterior);
            PairGeometry pairGeometry(FittedSupport &a, FittedSupport &b, bool direct);
            double extension(cv::Point2d point, const FittedSupport &fit) const;

            double connectionPrefix(size_t begin, size_t end) const
            {
                return end >= begin ? lengths_[end] - lengths_[begin]
                                    : lengths_.back() - lengths_[begin] + lengths_[end];
            }

            EdgePairFitResult stage(bool all, bool posterior, LineFitModel model);
            void select(EdgePairFitResult &result, const ArcAlias &a, const ArcAlias &b,
                        bool direct, const PairGeometry &geometry, size_t stage);

            const std::vector<cv::Point> &contour_;
            const ModelEdges &edges_;
            const CornerConfig &config_;
            const CornerObservationBudget &budget_;
            const EdgePairFitStrategy strategy_;
            EdgePairFitTrace *const trace_;
            size_t stage_number_{0};
            double winding_{0}, length_roundoff_{0};
            std::vector<cv::Point> polygon_;
            std::vector<size_t> original_indices_, first_occurrences_;
            std::vector<double> lengths_;
            std::array<std::vector<size_t>, 2> bad_positions_;
            std::vector<std::optional<std::vector<size_t>>> exclusions_;
            std::array<std::optional<std::vector<SupportRange>>, 2> descriptors_;
            std::unordered_map<size_t, SupportRecord> supports_;
            // 三元键 (modelKind,firstRaw,lastRaw)；拒绝结果同样缓存，引用只活在本实例。
            std::array<std::unordered_map<size_t, FittedSupport>, 6> fits_;
            // 同一调用的周期估计器共享向量容量；输入与参数仍每条弧从头计算，不跨帧共享。
            PeriodicLineFitScratch periodic_scratch_;
        };

        FittedSupport &EdgePairFitter::fitFor(SupportRecord &support, LineFitModel model)
        {
            auto entry = fits_[static_cast<size_t>(model)].try_emplace(support.id);
            auto &fit = entry.first->second;
            if (!entry.second)
                return fit;
            fit.support = &support;
            fit.model = model;
            if (!support.unique_ready)
            {
                // 原拟合用全部像素；此处去重仅证明点数下限，达到即停，不改变拟合权重。
                std::set<std::pair<double, double>> unique;
                for (auto point : support.points)
                {
                    unique.emplace(point.x, point.y);
                    if (unique.size() >= static_cast<size_t>(config_.min_line_points_))
                    {
                        support.enough_unique = true;
                        break;
                    }
                }
                support.unique_ready = true;
            }
            if (!support.enough_unique)
                return fit;
            if (model == LineFitModel::Legacy)
                cv::fitLine(support.points, fit.line, cv::DIST_L2, 0, 0.01, 0.01);
            else
            {
                const auto periodic = fitPeriodicLine(support.points, model, periodic_scratch_);
                if (!periodic)
                    return fit;
                fit.line = periodic->line;
                fit.axis = periodic->axis;
                fit.coefficient = periodic->coefficient;
            }
            const double norm = std::hypot(fit.line[0], fit.line[1]);
            if (!(norm > 0))
                return fit;
            fit.line[0] /= norm;
            fit.line[1] /= norm;
            const cv::Point2d direction{fit.line[0], fit.line[1]}, origin{fit.line[2], fit.line[3]};
            double lo = INFINITY, hi = -INFINITY, sum = 0, model_sum = 0;
            fit.maximum = 0;
            fit.model_maximum = 0;
            for (auto point : support.points)
            {
                // 残差循环顺便记录 AABB；raw 与模型垂距分别求和，旧字段/排序口径不变。
                fit.minimum.x = std::min(fit.minimum.x, point.x);
                fit.minimum.y = std::min(fit.minimum.y, point.y);
                fit.maximum_point.x = std::max(fit.maximum_point.x, point.x);
                fit.maximum_point.y = std::max(fit.maximum_point.y, point.y);
                const double t = (point - origin).dot(direction);
                lo = std::min(lo, t);
                hi = std::max(hi, t);
                const double d = std::abs(direction.x * (point.y - origin.y) -
                                          direction.y * (point.x - origin.x));
                sum += d;
                fit.maximum = std::max(fit.maximum, d);
                const double corrected =
                    model == LineFitModel::Legacy
                        ? d
                        : periodicLineDistance(point, fit.line, fit.axis, fit.coefficient);
                model_sum += corrected;
                fit.model_maximum = std::max(fit.model_maximum, corrected);
            }
            fit.mean = sum / support.points.size();
            fit.model_mean = model_sum / support.points.size();
            fit.valid = hi - lo >= budget_.min_support_span_px &&
                        fit.model_mean <= config_.max_line_fit_error_;
            return fit;
        }

        unsigned EdgePairFitter::matches(FittedSupport &fit, unsigned position_mask, bool posterior)
        {
            auto &cached = fit.matches[posterior];
            if (cached)
                return *cached;
            unsigned mask = 0;
            const cv::Point2d direction{fit.line[0], fit.line[1]}, origin{fit.line[2], fit.line[3]};
            for (unsigned edge = 0; edge < 2; ++edge)
            {
                if ((!posterior && !(position_mask & (1u << edge))) ||
                    observed::angleDeg(direction, edges_[edge][1] - edges_[edge][0]) >
                        budget_.max_edge_direction_diff_deg)
                    continue;
                bool good = true;
                // 原位置已由区间坏点数严格证明，无需再次逐点扫描；后验仍投影全部实测点。
                if (posterior)
                    for (auto point : fit.support->points)
                    {
                        const auto projected = origin + direction * (point - origin).dot(direction);
                        if (observed::segmentDistance(projected, edges_[edge][0], edges_[edge][1]) >
                            budget_.max_edge_position_distance_px)
                        {
                            good = false;
                            break;
                        }
                    }
                if (good)
                    mask |= 1u << edge;
            }
            cached = mask;
            return mask;
        }

        double EdgePairFitter::extension(cv::Point2d point, const FittedSupport &fit) const
        {
            const cv::Point2d direction{fit.line[0], fit.line[1]};
            double lo = INFINITY, hi = -INFINITY;
            // 只有严格轴向单位线才用 AABB 极值，其他方向保持原逐点投影，不能近似判轴向。
            if (direction.x == 0 && std::abs(direction.y) == 1)
            {
                lo = direction.y > 0 ? fit.minimum.y - point.y : point.y - fit.maximum_point.y;
                hi = direction.y > 0 ? fit.maximum_point.y - point.y : point.y - fit.minimum.y;
            }
            else if (direction.y == 0 && std::abs(direction.x) == 1)
            {
                lo = direction.x > 0 ? fit.minimum.x - point.x : point.x - fit.maximum_point.x;
                hi = direction.x > 0 ? fit.maximum_point.x - point.x : point.x - fit.minimum.x;
            }
            else
                for (auto support : fit.support->points)
                {
                    const double t = (support - point).dot(direction);
                    lo = std::min(lo, t);
                    hi = std::max(hi, t);
                }
            return lo > 0 ? lo : hi < 0 ? -hi : 0;
        }

        PairGeometry EdgePairFitter::pairGeometry(FittedSupport &a, FittedSupport &b, bool direct)
        {
            PairGeometry pair;
            const auto &pa = a.support->points;
            const auto &pb = b.support->points;
            // 无 AABB 交集则不可能共享像素；只有相交时才惰性构造共享的完整集合。
            if (!(a.maximum_point.x < b.minimum.x || b.maximum_point.x < a.minimum.x ||
                  a.maximum_point.y < b.minimum.y || b.maximum_point.y < a.minimum.y))
            {
                auto &membership = a.support->membership;
                if (!membership)
                {
                    membership.emplace();
                    for (auto point : pa)
                        membership->emplace(point.x, point.y);
                }
                for (auto point : pb)
                    if (membership->count({point.x, point.y}))
                    {
                        pair.rejection = Rejection::ReusedSupport;
                        return pair;
                    }
            }
            size_t k = a.support->legacy_back, stop = b.support->legacy_front;
            if (connectionPrefix(k, stop) >
                budget_.max_turn_connection_length_px + length_roundoff_)
            {
                pair.rejection = Rejection::Connection;
                return pair;
            }
            double length = 0;
            for (;;)
            {
                const cv::Point2d point = contour_[k];
                if (!pair.connector.empty())
                    length += cv::norm(point - pair.connector.back());
                pair.connector.push_back(point);
                if (k == stop || length > budget_.max_turn_connection_length_px)
                    break;
                k = (k + 1) % contour_.size();
            }
            if (k != stop || length > budget_.max_turn_connection_length_px)
            {
                pair.rejection = Rejection::Connection;
                return pair;
            }
            const cv::Point2d da = pa.back() - pa.front(), db = pb.back() - pb.front();
            if ((da.x * db.y - da.y * db.x) * winding_ <= 0)
            {
                pair.rejection = Rejection::Nonconvex;
                return pair;
            }
            const auto &first = direct ? a : b;
            const auto &second = direct ? b : a;
            const cv::Point2d v{first.line[0], first.line[1]}, w{second.line[0], second.line[1]};
            const double angle = observed::angleDeg(v, w);
            if (!std::isfinite(angle) || angle < config_.min_intersection_angle_deg_)
            {
                pair.rejection = Rejection::Angle;
                return pair;
            }
            const double cross = v.x * w.y - v.y * w.x;
            const cv::Point2d origin{first.line[2], first.line[3]},
                other{second.line[2], second.line[3]}, delta = other - origin;
            pair.intersection = origin + v * ((delta.x * w.y - delta.y * w.x) / cross);
            if (!observed::finite(pair.intersection))
                return pair;
            pair.corner_error =
                observed::boundaryDistance(pair.intersection, pair.connector, false);
            pair.first_extension = extension(pair.intersection, first);
            pair.second_extension = extension(pair.intersection, second);
            if (pair.corner_error > config_.max_corner_error_ ||
                pair.first_extension > budget_.max_support_extension_px ||
                pair.second_extension > budget_.max_support_extension_px)
            {
                pair.rejection = Rejection::CornerOrExtension;
                return pair;
            }
            pair.rejection = Rejection::Passed;
            return pair;
        }

        void EdgePairFitter::select(EdgePairFitResult &result, const ArcAlias &a, const ArcAlias &b,
                                    bool direct, const PairGeometry &geometry, size_t stage)
        {
            const auto &first_alias = direct ? a : b;
            const auto &second_alias = direct ? b : a;
            const auto &first = *first_alias.fit;
            const auto &second = *second_alias.fit;
            if (trace_)
                trace_->pairs.push_back(
                    {stage,
                     {first_alias.begin, first_alias.end, second_alias.begin, second_alias.end},
                     geometry.intersection,
                     geometry.connector,
                     geometry.corner_error,
                     geometry.first_extension,
                     geometry.second_extension});
            // 几何 memo 不包含 best：每个成功别名无条件检查歧义，然后才评分/tie-break。
            if (result.evidence &&
                cv::norm(result.evidence->intersection_ - geometry.intersection) *
                        cv::norm(result.evidence->intersection_ - geometry.intersection) >
                    config_.semantic_geometry_threshold_)
            {
                result.evidence.reset();
                result.reason = "AMBIGUOUS_EDGE_PAIR: 外边观测解几何冲突";
                return;
            }
            const double error = first.mean + second.mean;
            const std::array<int, 2> ids{
                {static_cast<int>(first_alias.begin), static_cast<int>(second_alias.begin)}},
                ends{{static_cast<int>(first_alias.end), static_cast<int>(second_alias.end)}};
            if (result.evidence)
            {
                const std::array<int, 2> previous{{result.evidence->observed_segment_ids_[0],
                                                   result.evidence->observed_segment_ids_[1]}};
                if (!(error < result.evidence->error_ ||
                      (error == result.evidence->error_ &&
                       std::tie(ids, ends) <
                           std::tie(previous, result.evidence->observed_segment_end_ids_))))
                    return;
            }
            // 仅最佳取证深拷贝；原始支持与连接弧完整保存，不用拟合参数替代像素证据。
            CornerEvidence evidence{};
            evidence.original_observation_ = true;
            evidence.truncated_ = false;
            evidence.original_support_arcs_ = {first.support->points, second.support->points};
            evidence.original_turn_arc_ = geometry.connector;
            evidence.observed_segment_ids_.assign(ids.begin(), ids.end());
            evidence.observed_segment_end_ids_ = ends;
            evidence.edge_segment_a_ = {first.support->points.front(),
                                        first.support->points.back()};
            evidence.edge_segment_b_ = {second.support->points.front(),
                                        second.support->points.back()};
            evidence.line_a_ = first.line;
            evidence.line_b_ = second.line;
            evidence.intersection_ = geometry.intersection;
            evidence.line_mean_residual_px_ = {first.mean, second.mean};
            evidence.line_max_residual_px_ = {first.maximum, second.maximum};
            evidence.line_model_kinds_ = {first.model, second.model};
            evidence.periodic_axes_ = {first.axis, second.axis};
            evidence.periodic_coefficients_ = {first.coefficient, second.coefficient};
            evidence.model_mean_residual_px_ = {first.model_mean, second.model_mean};
            evidence.model_max_residual_px_ = {first.model_maximum, second.model_maximum};
            evidence.support_extension_px_ = {geometry.first_extension, geometry.second_extension};
            evidence.corner_error_px_ = geometry.corner_error;
            evidence.error_ = error;
            result.evidence = std::move(evidence);
        }

        EdgePairFitResult EdgePairFitter::stage(bool all, bool posterior, LineFitModel model)
        {
            EdgePairFitResult result;
            const size_t stage = stage_number_++;
            std::array<size_t, 8> rejected{};
            std::vector<ArcAlias> arcs;
            std::set<size_t> ranges_seen;
            std::set<const std::vector<cv::Point2d> *, SequenceLess> sequences_seen;
            for (const auto &range : descriptors(all))
            {
                if (!range.continuous)
                {
                    ++rejected[static_cast<size_t>(Rejection::Discontinuous)];
                    continue;
                }
                if (!posterior && !range.position_mask)
                {
                    ++rejected[static_cast<size_t>(Rejection::DirectionOrPosition)];
                    continue;
                }
                if (range.empty)
                {
                    ++rejected[static_cast<size_t>(Rejection::ShortOrResidual)];
                    continue;
                }
                // 原去重点数至少为 min；访问数更少是严格必要条件，尚未展开像素即可拒绝。
                if ((range.last + contour_.size() - range.first) % contour_.size() + 1 <
                    static_cast<size_t>(config_.min_line_points_))
                {
                    ++rejected[static_cast<size_t>(Rejection::ShortOrResidual)];
                    continue;
                }
                auto &support = supportFor(range);
                // 全端点先排严格相同 raw range，再按全部有序像素排不同 range 的同序列。
                if (all && (!ranges_seen.insert(support.id).second ||
                            !sequences_seen.insert(&support.points).second))
                    continue;
                auto &fit = fitFor(support, model);
                if (!fit.valid)
                {
                    ++rejected[static_cast<size_t>(Rejection::ShortOrResidual)];
                    continue;
                }
                const unsigned mask = matches(fit, range.position_mask, posterior);
                if (!mask)
                {
                    ++rejected[static_cast<size_t>(Rejection::DirectionOrPosition)];
                    continue;
                }
                if (trace_)
                    trace_->arcs.push_back({stage, range.begin, range.end, mask, fit.line,
                                            support.points, fit.mean, fit.maximum, fit.model_mean,
                                            fit.model_maximum, model, fit.axis, fit.coefficient});
                arcs.push_back({range.begin, range.end, &fit, mask});
            }

            // 凸转折必须沿原轮廓正向连接；按 legacy 首次 front 索引建桶，不按空间近邻配对。
            std::vector<std::vector<size_t>> fronts(contour_.size());
            for (size_t j = 0; j < arcs.size(); ++j)
                fronts[arcs[j].fit->support->legacy_front].push_back(j);
            std::unordered_map<PairKey, PairGeometry, PairHash> pairs;
            std::vector<size_t> candidates;
            candidates.reserve(arcs.size());
            for (size_t i = 0; i < arcs.size(); ++i)
            {
                const auto &a = arcs[i];
                const size_t begin = a.fit->support->legacy_back;
                candidates.clear();
                for (size_t step = 0; step < contour_.size(); ++step)
                {
                    const size_t front = (begin + step) % contour_.size();
                    if (connectionPrefix(begin, front) >
                        budget_.max_turn_connection_length_px + length_roundoff_)
                        break;
                    candidates.insert(candidates.end(), fronts[front].begin(), fronts[front].end());
                }
                // 桶只排必然不相邻者；合格候选仍恢复原 j 顺序，确保评分与歧义顺序不变。
                std::sort(candidates.begin(), candidates.end());
                for (auto j : candidates)
                {
                    if (i == j)
                        continue;
                    const auto &b = arcs[j];
                    const bool direct = (a.edge_mask & 1) && (b.edge_mask & 2),
                               reverse = (a.edge_mask & 2) && (b.edge_mask & 1);
                    if (!direct && !reverse)
                        continue;
                    const PairKey key{a.fit->support->id, b.fit->support->id};
                    auto entry = pairs.try_emplace(key);
                    if (entry.second)
                        entry.first->second = pairGeometry(*a.fit, *b.fit, direct);
                    const auto &geometry = entry.first->second;
                    if (geometry.rejection != Rejection::Passed)
                    {
                        const auto reason = static_cast<size_t>(geometry.rejection);
                        if (reason < rejected.size())
                            ++rejected[reason];
                        continue;
                    }
                    select(result, a, b, direct, geometry, stage);
                    if (!result.reason.empty())
                        return result;
                }
            }
            if (!result.evidence)
            {
                std::ostringstream reason;
                reason << "NO_VALID_ADJACENT_EDGES: polygon=" << polygon_.size()
                       << ",eligible_arcs=" << arcs.size();
                for (size_t k = 0; k < rejected.size(); ++k)
                    if (rejected[k])
                        reason << ',' << rejection_names[k] << '=' << rejected[k];
                result.reason = reason.str();
            }
            return result;
        }

        EdgePairFitResult EdgePairFitter::run()
        {
            if (polygon_.size() < 3)
                return {std::nullopt, "SHORT_SUPPORT: 原图轮廓不足"};
            auto result = stage(false, false, LineFitModel::Legacy);
            if (result.evidence || strategy_ == EdgePairFitStrategy::OriginalOnly)
                return result;
            // 快速路线优先验证原端点上的合法周期候选；旧成功依然最先返回。
            if (strategy_ == EdgePairFitStrategy::FastRecovery)
                for (bool posterior : {false, true})
                    for (auto model :
                         {LineFitModel::PeriodicPhaseMedian, LineFitModel::PeriodicZeroSlope})
                    {
                        result = stage(false, posterior, model);
                        if (result.evidence)
                            return result;
                    }
            result = stage(false, true, LineFitModel::Legacy);
            if (result.evidence)
                return result;
            result = stage(true, false, LineFitModel::Legacy);
            if (result.evidence || strategy_ == EdgePairFitStrategy::LinearRecovery)
                return result;
            // 完整 B/A/C/L1 reference 路由保留；无资源上限、超时熔断或针对帧号的特殊分支。
            for (auto model : {LineFitModel::PeriodicPhaseMedian, LineFitModel::PeriodicL1})
                for (auto mode :
                     {std::pair<bool, bool>{false, false}, {false, true}, {true, false}})
                {
                    result = stage(mode.first, mode.second, model);
                    if (result.evidence)
                        return result;
                }
            return result;
        }
    }

    EdgePairFitResult fitObservedEdgePair(const std::vector<cv::Point> &contour,
                                          const ModelEdges &edges, const CornerConfig &config,
                                          EdgePairFitStrategy strategy, EdgePairFitTrace *trace)
    {
        validateCornerObservationBudget(config);
        EdgePairFitter fitter(contour, edges, config, strategy, trace);
        return fitter.run();
    }

    EdgePairFitResult fitObservedEdgePair(const std::vector<cv::Point> &contour,
                                          const ModelEdges &edges, const CornerConfig &config,
                                          EdgePairFitStrategy strategy)
    {
        return fitObservedEdgePair(contour, edges, config, strategy, nullptr);
    }

    EdgePairFitResult fitObservedEdgePair(const std::vector<cv::Point> &contour,
                                          const ModelEdges &edges, const CornerConfig &config)
    {
        // 普通入口固定采用批准的快速路线；实验 reference 策略仅由内部测试显式选择。
        return fitObservedEdgePair(contour, edges, config, EdgePairFitStrategy::FastRecovery);
    }
}
