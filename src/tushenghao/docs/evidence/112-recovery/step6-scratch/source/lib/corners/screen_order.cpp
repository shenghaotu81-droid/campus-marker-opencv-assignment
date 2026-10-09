#include "corners/screen_order.hpp"
#include "corners/corner_types.hpp"
#include "mark/detector_config.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

// 输入 P0～P3 物理环，输出冻结的屏幕四循环排序；非法/退化输入返回 FAILED。
// 排序不测角、不补点、不借历史；平局集合固定相对于全局最小能量。

namespace mark
{

    // 10个helper + ScreenCandidate
    namespace
    {

        // Part1: 6 个工具函数:输入检查+生成候选+归一化(给主函数 orderScreenCorners 打下手，不含业务逻辑)
        // 判重点
        /**
         * @brief 判断两个点是否相同。
         *
         * 排序要求输入必须是四个独立物理角。
         *
         * 如果两个物理角落在同一个位置，
         * 后续归一化和屏幕排序没有意义。
         */
        bool same_point(
            const cv::Point2d &a,
            const cv::Point2d &b)
        {
            constexpr double epsilon = 1e-9;

            return std::abs(a.x - b.x) < epsilon &&
                   std::abs(a.y - b.y) < epsilon;
        }

        // 算面积看退化
        /**
         * @brief 计算四边形有向面积。
         *
         * 公式：
         *
         * area =
         * 1/2 Σ(x_i*y_(i+1)-x_(i+1)*y_i)
         *
         * 用于判断：
         * 1. 是否退化；
         * 2. 当前点序方向。
         *
         * Step 4 只调整屏幕顺序方向，
         * 不重新解释 P0~P3 身份。
         */
        double signed_area(
            const std::array<cv::Point2d, 4> &points)
        {
            double area = 0.0;

            for (size_t i = 0;
                 i < 4;
                 ++i)
            {
                const auto &a = points[i];

                const auto &b =
                    points[(i + 1) % 4];

                area +=
                    a.x * b.y -
                    b.x * a.y;
            }

            return area * 0.5;
        }

        // 检查四点是不是凸四边形
        /**
         * @brief 检查四点是否构成凸四边形。
         *
         * Step 4 不负责重新找角。
         *
         * 输入已经是：
         *
         * P0,P1,P2,P3
         *
         * 所以这里只检查这组物理角是否可以形成有效屏幕环。
         */
        bool is_convex(
            const std::array<cv::Point2d, 4> &points)
        {
            double last_cross = 0.0;

            for (size_t i = 0;
                 i < 4;
                 ++i)
            {
                cv::Point2d a =
                    points[i];

                cv::Point2d b =
                    points[(i + 1) % 4];

                cv::Point2d c =
                    points[(i + 2) % 4];

                cv::Point2d ab =
                    b - a;

                cv::Point2d bc =
                    c - b;

                double cross =
                    ab.x * bc.y -
                    ab.y * bc.x;

                if (!std::isfinite(cross) || std::abs(cross) < 1e-9)
                {
                    return false;
                }

                if (i == 0)
                {
                    last_cross = cross;
                }
                else
                {
                    /*
                     * 凸四边形要求所有转向一致。
                     *
                     * 如果符号变化，说明存在凹陷。
                     */
                    if (cross * last_cross < 0)
                    {
                        return false;
                    }
                }
            }

            return true;
        }

        // 只做 4 个循环起点（P0开头、P1开头…）
        /**
         * @brief 生成循环起点对应的候选点序。
         *
         * 这里只允许 4 个循环起点。
         *
         * 原因：
         * physical_corners 已经固定：
         *
         * [0]=P0
         * [1]=P1
         * [2]=P2
         * [3]=P3
         *
         * Step 4 只决定屏幕显示从哪个角开始，
         * 不允许 permutation 重新排列物理身份。
         */
        std::array<cv::Point2d, 4> rotate_start(
            const std::array<cv::Point2d, 4> &points,
            int start)
        {
            std::array<cv::Point2d, 4> result;

            for (int i = 0;
                 i < 4;
                 ++i)
            {
                result[i] =
                    points[(start + i) % 4];
            }

            return result;
        }

        // 保证是顺时针
        /**
         * @brief 保证候选点序为顺时针。
         *
         * signed_area:
         *
         * < 0:
         * 顺时针
         *
         * > 0:
         * 逆时针
         *
         * 如果需要反向，只调整候选屏幕顺序。
         *
         * 不改变 physical_to_screen。
         */
        std::array<cv::Point2d, 4> make_clockwise(
            const std::array<cv::Point2d, 4> &input)
        {
            auto result = input;

            if (signed_area(result) < 0)  // y朝下时，<0 才是逆时针，需要反转(测试debug)
            {
                std::reverse(
                    result.begin(),
                    result.end());
            }

            return result;
        }

        // 把坐标缩到 [0,1]，消除平移缩放影响，范围退化直接返回 false
        /**
         * @brief 归一化屏幕坐标。
         *
         * 将当前候选四边形映射到：
         *
         * xmin -> 0
         * xmax -> 1
         *
         * ymin -> 0
         * ymax -> 1
         *
         * 目的：
         * 消除平移和缩放影响。
         *
         * 注意：
         * 如果范围退化：
         *
         * xmax==xmin
         * 或
         * ymax==ymin
         *
         * 必须直接失败。
         */
        bool normalize_points(
            const std::array<cv::Point2d, 4> &input,
            std::array<cv::Point2d, 4> &output)
        {
            double xmin =
                input[0].x;

            double xmax =
                input[0].x;

            double ymin =
                input[0].y;

            double ymax =
                input[0].y;

            for (const auto &point : input)
            {
                xmin =
                    std::min(
                        xmin,
                        point.x);

                xmax =
                    std::max(
                        xmax,
                        point.x);

                ymin =
                    std::min(
                        ymin,
                        point.y);

                ymax =
                    std::max(
                        ymax,
                        point.y);
            }

            if (xmax == xmin || ymax == ymin ||
                !std::isfinite(xmax-xmin) || !std::isfinite(ymax-ymin))
            {
                return false;
            }

            for (size_t i = 0;
                 i < 4;
                 ++i)
            {
                output[i].x =
                    (input[i].x - xmin) /
                    (xmax - xmin);

                output[i].y =
                    (input[i].y - ymin) /
                    (ymax - ymin);
            }

            return true;
        }

        // Part2: 4 个工具函数 + 主函数(算能量+建映射+主函数)
        // 按冻结公式算 E（跟 LT/RT/RB/LB 参考点的平方和）
        /**
         * @brief 计算归一化候选的 E 值。
         *
         * 参考角固定为：
         *
         * LT = (0,0)
         * RT = (1,0)
         * RB = (1,1)
         * LB = (0,1)
         *
         * E =
         *
         * Σ ||p_i - a_i||²
         *
         * i=0..3
         *
         * 使用平方和：
         * - 不需要开根号；
         * - 避免不同方向误差抵消；
         * - 数值更稳定。
         *
         * E 越小，说明当前屏幕顺序越接近标准矩形布局。
         */
        double compute_energy(
            const std::array<cv::Point2d, 4> &normalized)
        {
            const std::array<cv::Point2d, 4> reference =
                {
                    cv::Point2d{0.0, 0.0}, // LT
                    cv::Point2d{1.0, 0.0}, // RT
                    cv::Point2d{1.0, 1.0}, // RB
                    cv::Point2d{0.0, 1.0}  // LB
            };

            double energy = 0.0;

            for (size_t i = 0;
                 i < 4;
                 ++i)
            {
                double dx =
                    normalized[i].x -
                    reference[i].x;

                double dy =
                    normalized[i].y -
                    reference[i].y;

                energy +=
                    dx * dx +
                    dy * dy;
            }

            return energy;
        }

        // 用 64*DBL_EPSILON 容差判平局，不用 ==
        /**
         * @brief 计算两个 E 值是否属于平局。
         *
         * 不能使用：
         *
         * E1 == E2
         *
         * 因为 double 浮点计算存在舍入误差。
         *
         * 冻结公式：
         *
         * tolerance =
         * 64 * DBL_EPSILON *
         * max(1.0, |E1|, |E2|)
         *
         * 当：
         *
         * |E1-E2| <= tolerance
         *
         * 才认为两个候选平局。
         */
        bool energy_tie(
            double e1,
            double e2)
        {
            double tolerance =
                64.0 *
                DBL_EPSILON *
                std::max(
                    {1.0,
                     std::abs(e1),
                     std::abs(e2)});

            return std::abs(e1 - e2) <= tolerance;
        }

        // 平局时按 (y,x) 字典序挑
        /**
         * @brief 按 (y,x) 字典序比较两个屏幕点序。
         *
         * 平局时不能静默选择第一个候选。
         *
         * 需要：
         * 1. 标记 screen_order_tie；
         * 2. 使用确定规则消除歧义。
         */
        bool lexicographically_less(
            const std::array<cv::Point2d, 4> &a,
            const std::array<cv::Point2d, 4> &b)
        {
            for (size_t i = 0;
                 i < 4;
                 ++i)
            {
                if (a[i].y != b[i].y)
                {
                    return a[i].y < b[i].y;
                }

                if (a[i].x != b[i].x)
                {
                    return a[i].x < b[i].x;
                }
            }

            return false;
        }

        // 生成 P0~P3→屏幕序号的映射
        /**
         * @brief 根据屏幕顺序生成 physical_to_screen。
         *
         * 输入：
         *
         * screen_order[screen_index]
         *
         * 对应原始 physical 点。
         *
         * 输出：
         *
         * physical index -> screen index
         *
         * 不重新解释物理身份。
         */
        std::array<int, 4> build_physical_to_screen(
            const std::array<cv::Point2d, 4> &physical,
            const std::array<cv::Point2d, 4> &screen)
        {
            std::array<int, 4> mapping =
                {
                    -1,
                    -1,
                    -1,
                    -1};

            for (size_t physical_index = 0;
                 physical_index < 4;
                 ++physical_index)
            {
                for (size_t screen_index = 0;
                     screen_index < 4;
                     ++screen_index)
                {
                    if (same_point(
                            physical[physical_index],
                            screen[screen_index]))
                    {
                        mapping[physical_index] =
                            static_cast<int>(
                                screen_index);

                        break;
                    }
                }
            }

            return mapping;
        }

        // 临时小篮子（4个候选再到后面的step比较）
        struct ScreenCandidate
        {
            std::array<cv::Point2d, 4> points_; // 这个候选的 4 个点排成啥样

            double energy_; // 算出来的 E 值，越小越接近标准矩形

            std::array<int, 4> physical_to_screen_; // 这个候选对应的 P0~P3→屏幕映射
        };

    } // namespace

    // 主函数：orderScreenCorners 走 8 步——验输入、验凸性、枚举 4 个循环起点、转顺时针、归一化、算 E 找最小、平局处理、组装 ScreenOrder 输出
    /**
     * @brief 将物理角点转换成屏幕顺序。
     *
     * 输入已经明确：
     *
     * physical_corners[0..3]
     *
     * 分别表示：
     *
     * P0~P3。
     *
     * 这里不重新识别角点，
     * 只解决：
     *
     * "当前四个物理角，在屏幕上应该怎样排列"
     *
     * config 当前没有排序参数，
     * 但保留接口以保持 Block 3 API 一致。
     */
    static ScreenOrderResult orderCycleCore(
        const std::array<cv::Point2d, 4> &physical_corners,
        const CornerConfig &config)
    {
        (void)config;

        ScreenOrderResult result;

        result.status_ =
            ScreenOrderStatus::FAILED;

        /*
         * Step 0:
         * 输入检查。
         *
         * 重复点或者完全退化四边形，
         * 后续没有可靠屏幕顺序。
         */
        for (size_t i = 0;
             i < 4;
             ++i)
        {
            // NaN 会穿过比较和凸性判断，须在任何几何运算之前明确拒绝。
            if (!std::isfinite(physical_corners[i].x) || !std::isfinite(physical_corners[i].y))
            {
                result.rejection_reason_ = "输入物理角点非有限";
                return result;
            }
            for (size_t j = i + 1;
                 j < 4;
                 ++j)
            {
                if (same_point(
                        physical_corners[i],
                        physical_corners[j]))
                {
                    result.rejection_reason_ =
                        "输入物理角点重复";

                    return result;
                }
            }
        }

        if (std::abs(
                signed_area(
                    physical_corners)) < 1e-9)
        {
            result.rejection_reason_ =
                "输入物理角点面积退化";

            return result;
        }

        /*
         * Step 1:
         * 凸性检查。
         */
        if (!is_convex(
                physical_corners))
        {
            result.rejection_reason_ =
                "四点不是凸四边形";

            return result;
        }

        std::vector<ScreenCandidate> candidates;

        /*
         * Step 2:
         * 只枚举四个循环起点。
         *
         * 不使用全排列。
         *
         * 原因：
         * P0~P3 的物理身份已经冻结，
         * 排序只负责屏幕顺序。
         */
        for (int start = 0;
             start < 4;
             ++start)
        {
            auto candidate =
                rotate_start(
                    physical_corners,
                    start);

            /*
             * Step 3:
             * 转成顺时针环。
             */
            candidate =
                make_clockwise(
                    candidate);

            /*
             * Step 4:
             * 归一化。
             *
             * 退化范围直接失败。
             */
            std::array<cv::Point2d, 4> normalized;

            if (!normalize_points(
                    candidate,
                    normalized))
            {
                result.rejection_reason_ =
                    "归一化范围退化";

                return result;
            }

            /*
             * Step 5:
             * 计算 E。
             */
            double energy =
                compute_energy(
                    normalized);

            candidates.push_back(
                {candidate,
                 energy,
                 build_physical_to_screen(
                     physical_corners,
                     candidate)});
        }

        if (candidates.empty())
        {
            result.rejection_reason_ =
                "没有有效屏幕顺序候选";

            return result;
        }

        /*
         * Step 6:
         * 找最小 E。
         */
        size_t best_index = static_cast<size_t>(std::min_element(
            candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
                return a.energy_ < b.energy_;
            }) - candidates.begin());
        const double minimum_energy = candidates[best_index].energy_;
        size_t tied_count = 0;
        // 旧循环先严格小于再判容差，后来的微小下降会抹掉平局。
        // 先固定 E_min，再收集同一集合，字典序选择不改变集合基准。
        for (size_t i = 0; i < candidates.size(); ++i)
        {
            if (!energy_tie(candidates[i].energy_, minimum_energy)) continue;
            ++tied_count;
            if (lexicographically_less(candidates[i].points_, candidates[best_index].points_))
                best_index = i;
        }
        const bool tie = tied_count > 1;

        /*
         * Step 8:
         * 输出。
         */
        ScreenOrder order;
        order.screen_points_ = candidates[best_index].points_;
        order.physical_to_screen_ = candidates[best_index].physical_to_screen_;
        order.screen_order_tie_ = tie;

        result.status_ =
            ScreenOrderStatus::SUCCESS;

        result.screen_order_ = order;

        result.rejection_reason_.clear();

        return result;
    }

// 旧物理入口与新几何环入口共享同一排序核心；不复制能量/平局比较。
    ScreenOrderResult orderScreenCorners(const std::array<cv::Point2d,4>& physical,
                                         const CornerConfig& config) {
        return orderCycleCore(physical, config);
    }
    std::optional<ScreenCycleOrder> orderScreenCycle(const std::array<cv::Point2d,4>& cycle,
                                                    const CornerConfig& config, std::string& reason) {
        auto result = orderCycleCore(cycle, config);
        reason = result.rejection_reason_;
        if (!result.screen_order_) return std::nullopt;
        const auto& order = *result.screen_order_;
        return ScreenCycleOrder{order.screen_points_, order.physical_to_screen_, order.screen_order_tie_};
    }

} // namespace mark
