// 周期相位中位数模型；所有状态都是局部 scratch，不读取图像、帧号或环境。
#include "corners/fitting/periodic_line_fit.hpp"
#include "vision/math/observed_geometry.hpp"
#include <climits>

namespace mark
{
    namespace
    {
        // 工作向量由单次 fit 的显式 scratch 提供；validator wrapper 仍创建自己的独立 scratch。
        using ScanPoint = PeriodicLineFitScratch::ScanPoint;
        using PhaseScratch = PeriodicLineFitScratch;

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

        // 固定斜率下用两相位中位数估计截距；scratch只复用向量容量。
        PeriodicLineFit phaseEstimate(const std::vector<ScanPoint> &points,
                                                         int axis, double center, double slope,
                                                         PhaseScratch &scratch)
        {
            for (auto &group : scratch.groups)
                group.clear();
            for (const auto &point : points)
                scratch.groups[point.h > 0].push_back(point.z - slope * (point.t - center));
            const double minus = median(scratch.groups[0]), plus = median(scratch.groups[1]);
            const double intercept = 0.5 * (minus + plus), coefficient = 0.5 * (plus - minus);
            return physicalLine(axis, slope, center, intercept, coefficient);
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

    std::optional<PeriodicLineFit> fitPeriodicLine(const std::vector<cv::Point2d> &support)
    {
        PeriodicLineFitScratch scratch;
        return fitPeriodicLine(support, scratch);
    }

    std::optional<PeriodicLineFit> fitPeriodicLine(const std::vector<cv::Point2d> &support,
                                                   PeriodicLineFitScratch &scratch)
    {
        if (support.size() < 3)
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
        auto &points = scratch.points;
        points.clear();
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
        for (auto &group : scratch.groups)
            group.reserve(points.size());
        const double slope = result.line[axis ? 0 : 1] / result.line[axis ? 1 : 0];
        result = phaseEstimate(points, axis, mt, slope, scratch);
        for (double value : result.line.val)
            if (!std::isfinite(value))
                return std::nullopt;
        return std::isfinite(result.coefficient) ? std::optional<PeriodicLineFit>(result)
                                                 : std::nullopt;
    }
}
