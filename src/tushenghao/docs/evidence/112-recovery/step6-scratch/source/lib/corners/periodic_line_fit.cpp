// 周期模型和三个估计器分别具名；所有状态都是局部 scratch，不读取图像、帧号或环境。
#include "corners/periodic_line_fit.hpp"
#include "core/observed_geometry_utils.hpp"
#include <map>
#include <set>
#include <climits>

namespace mark
{
    namespace
    {
        struct ScanPoint
        {
            double t, z, h;
        };

        struct PhaseScratch
        {
            std::array<std::vector<double>, 2> groups;
        };

        // 平均绝对残差对应中位数；偶数样本用中间两值平均，全部重复访问仍参与权重。
        double median(std::vector<double> &values)
        {
            auto middle = values.begin() + values.size() / 2;
            std::nth_element(values.begin(), middle, values.end());
            return values.size() % 2 ? *middle
                                     : 0.5 * (*middle + *std::max_element(values.begin(), middle));
        }

        PeriodicLineFit physicalLine(int axis, double slope, double center, double intercept,
                                     double coefficient)
        {
            const double norm = std::hypot(slope, 1.0);
            return {axis ? cv::Vec4d(slope / norm, 1 / norm, intercept, center)
                         : cv::Vec4d(1 / norm, slope / norm, center, intercept),
                    axis, coefficient};
        }

        // 固定斜率时消去两相位截距；反复求 L1 目标时复用向量，避免每次重新分配。
        std::pair<double, PeriodicLineFit> phaseEstimate(const std::vector<ScanPoint> &points,
                                                         int axis, double center, double slope,
                                                         PhaseScratch &scratch)
        {
            for (auto &group : scratch.groups)
                group.clear();
            for (const auto &point : points)
                scratch.groups[point.h > 0].push_back(point.z - slope * (point.t - center));
            const double minus = median(scratch.groups[0]), plus = median(scratch.groups[1]);
            const double intercept = 0.5 * (minus + plus), coefficient = 0.5 * (plus - minus);
            double loss = 0;
            for (const auto &point : points)
                loss += std::abs(point.z - slope * (point.t - center) - intercept -
                                 coefficient * point.h);
            return {loss, physicalLine(axis, slope, center, intercept, coefficient)};
        }

        // 消去截距后的坐标 L1 目标凸且分段线性；固定 40 次搜索，再精确复算区间拐点。
        PeriodicLineFit refineL1(const std::vector<ScanPoint> &points,
                                 const PeriodicLineFit &initial, PhaseScratch &scratch)
        {
            const int axis = initial.axis;
            const double center = initial.line[axis ? 3 : 2];
            const double slope = initial.line[axis ? 0 : 1] / initial.line[axis ? 1 : 0];
            auto evaluate = [&](double candidate)
            {
                return phaseEstimate(points, axis, center, candidate, scratch);
            };
            // 每个扫描坐标的中位损失是任意 a/b/c 的下界；仅严格相等才采用快速证书。
            std::map<double, std::vector<double>> scans;
            double min_z = INFINITY, max_z = -INFINITY;
            for (const auto &point : points)
            {
                scans[point.t].push_back(point.z);
                min_z = std::min(min_z, point.z);
                max_z = std::max(max_z, point.z);
            }
            double lower_bound = 0;
            for (auto &scan : scans)
            {
                const double center_z = median(scan.second);
                for (double z : scan.second)
                    lower_bound += std::abs(z - center_z);
            }
            const auto zero = evaluate(0);
            auto best = evaluate(slope);
            if (zero.first == lower_bound)
                return best.first <= zero.first ? best.second : zero.second;

            // 整数同相位的非零 dt 至少为 2，有限拐点均在 ±max(1,zmax-zmin) 内。
            const double bound = std::max(1.0, max_z - min_z);
            double lo = -bound, hi = bound;
            for (int iteration = 0; iteration < 40; ++iteration)
            {
                const double left = (2 * lo + hi) / 3, right = (lo + 2 * hi) / 3;
                if (evaluate(left).first <= evaluate(right).first)
                    hi = right;
                else
                    lo = left;
            }
            auto consider = [&](double candidate)
            {
                auto estimate = evaluate(candidate);
                if (estimate.first < best.first)
                    best = std::move(estimate);
            };
            // 严格小于才换解；保留原斜率、中心、零斜率和首次拐点的确定性顺序。
            consider((lo + hi) / 2);
            consider(0);
            std::set<double> seen;
            for (size_t i = 0; i < points.size(); ++i)
                for (size_t j = 0; j < i; ++j)
                {
                    if (points[i].h != points[j].h)
                        continue;
                    const double dt = points[i].t - points[j].t;
                    if (dt == 0)
                        continue;
                    const double knot = (points[i].z - points[j].z) / dt;
                    if (knot >= lo && knot <= hi && seen.insert(knot).second)
                        consider(knot);
                }
            return best.second;
        }
    }

    double scanPhase(cv::Point2d point, int axis)
    {
        const double coordinate = axis ? point.y : point.x;
        // 原始像素为整数，精确转换避免 libm；非整数仍使用原 llround 语义，非法值拒绝。
        if (coordinate >= INT_MIN && coordinate <= INT_MAX)
        {
            const int integer = static_cast<int>(coordinate);
            if (coordinate == integer)
                return (integer & 1) ? 1.0 : -1.0;
        }
        if (!std::isfinite(coordinate) || coordinate < static_cast<double>(LLONG_MIN) ||
            coordinate >= static_cast<double>(LLONG_MAX))
            return std::numeric_limits<double>::quiet_NaN();
        return (std::llround(coordinate) & 1) ? 1.0 : -1.0;
    }

    double periodicLineDistance(cv::Point2d point, const cv::Vec4d &line, int axis,
                                double coefficient)
    {
        // c 的单位是正交坐标 px，先投影到法线；门控仍是平均垂距 0.5，不是 raw 误差。
        const double raw = line[0] * (point.y - line[3]) - line[1] * (point.x - line[2]);
        return std::abs(raw - (axis ? -line[1] : line[0]) * coefficient * scanPhase(point, axis));
    }

    std::optional<PeriodicLineFit> fitPeriodicLine(const std::vector<cv::Point2d> &support,
                                                   LineFitModel model)
    {
        if (support.size() < 3 ||
            (model != LineFitModel::PeriodicLeastSquares &&
             model != LineFitModel::PeriodicPhaseMedian && model != LineFitModel::PeriodicL1 &&
             model != LineFitModel::PeriodicZeroSlope))
            return std::nullopt;
        cv::Point2d minimum{INFINITY, INFINITY}, maximum{-INFINITY, -INFINITY};
        for (auto point : support)
        {
            if (!observed::finite(point))
                return std::nullopt;
            minimum.x = std::min(minimum.x, point.x);
            minimum.y = std::min(minimum.y, point.y);
            maximum.x = std::max(maximum.x, point.x);
            maximum.y = std::max(maximum.y, point.y);
        }
        const int axis = maximum.y - minimum.y >= maximum.x - minimum.x ? 1 : 0;
        std::vector<ScanPoint> points;
        points.reserve(support.size());
        double mt = 0, mz = 0, mh = 0;
        size_t plus_count = 0;
        for (auto point : support)
        {
            const ScanPoint scan{axis ? point.y : point.x, axis ? point.x : point.y,
                                 scanPhase(point, axis)};
            if (!std::isfinite(scan.h))
                return std::nullopt;
            points.push_back(scan);
            mt += scan.t;
            mz += scan.z;
            mh += scan.h;
            plus_count += scan.h > 0;
        }
        if (!plus_count || plus_count == points.size())
            return std::nullopt;
        mt /= points.size();
        mz /= points.size();
        mh /= points.size();
        // 中心化充分统计量闭式解 2×2，避免每弧通用矩阵；秩阈值只描述机器精度。
        double tt = 0, hh = 0, th = 0, tz = 0, hz = 0;
        for (const auto &point : points)
        {
            const double t = point.t - mt, h = point.h - mh, z = point.z - mz;
            tt += t * t;
            hh += h * h;
            th += t * h;
            tz += t * z;
            hz += h * z;
        }
        const double determinant = tt * hh - th * th;
        if (!(determinant > 64 * std::numeric_limits<double>::epsilon() * std::max(1.0, tt * hh)))
            return std::nullopt;
        const double a = (tz * hh - hz * th) / determinant, c = (hz * tt - tz * th) / determinant,
                     b = mz - c * mh;
        if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c))
            return std::nullopt;
        auto result = physicalLine(axis, a, mt, b, c);
        if (model == LineFitModel::PeriodicLeastSquares)
            return result;
        PhaseScratch scratch;
        for (auto &group : scratch.groups)
            group.reserve(points.size());
        const double slope = result.line[axis ? 0 : 1] / result.line[axis ? 1 : 0];
        // 零斜率是同一模型的 a=0 子空间；仍要求完整支持和两相位/满秩，不宣称必然最优。
        result = phaseEstimate(points, axis, mt,
                               model == LineFitModel::PeriodicZeroSlope ? 0 : slope, scratch)
                     .second;
        if (model == LineFitModel::PeriodicL1)
            result = refineL1(points, result, scratch);
        for (double value : result.line.val)
            if (!std::isfinite(value))
                return std::nullopt;
        return std::isfinite(result.coefficient) ? std::optional<PeriodicLineFit>(result)
                                                 : std::nullopt;
    }
}
