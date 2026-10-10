// 最终质检：检查 4 个角点是不是合法凸四边形、映射对不对、有没有出界，行就过，不行就给原因。
/*这个 cpp 是 Block3 Step 6 的最终质检实现，分两部分：

7 个工具函数（匿名 namespace）：
1. cross：二维叉积
2. orientation：三点转向判断
3. segments_intersect：两线段是否相交
4. is_convex：4 点是否凸（4 个转弯同向）
5. validate_mapping：physical_to_screen_ 是否合法（范围+不重复）
6. reorder_points：按映射把 P0~P3 重排成 LT/RT/RB/LB
7. inside_image：点是否在图内

主函数 8 步：
1. 角点非有限→拒
2. 映射非法→拒（先验后用，防越界）
3. 重建屏幕顺序点
4. 边退化（长度≤0）→拒
5. 非凸→拒
6. 自交→拒
7. 出界→拒
8. 全过→valid_=true

只做验证，不创建 Detection。
*/
/* 伪代码顺序：
finite
 ↓
physical_to_screen 校验
 ↓
重建 LT/RT/RB/LB
 ↓
退化
 ↓
凸性
 ↓
自交
 ↓
越界
 ↓
SUCCESS
*/
#include "corners/detection_validator.hpp"
#include "corners/corner_evidence_validation.hpp"
#include "core/corner_budget.hpp"
#include <set>

#include <algorithm>
#include <array>
#include <cmath>

namespace mark
{

    namespace
    {

        /**
         * @brief 二维向量叉积。
         *
         * 公式：
         *
         * cross(a,b)=a.x*b.y-a.y*b.x
         *
         * 用于判断：
         *
         * - 四边形转向；
         * - 凸性；
         * - 线段相交。
         */
        double cross(const cv::Point2d &a, const cv::Point2d &b)
        {
            return a.x * b.y - a.y * b.x;
        }

        /**
         * @brief 三点方向判断。
         *
         * 计算：
         *
         * cross(B-A,C-A)
         *
         * 结果：
         *
         * >0:
         * C 在 AB 左侧
         *
         * <0:
         * C 在 AB 右侧
         *
         * =0:
         * 三点共线
         */
        double orientation(const cv::Point2d &a, const cv::Point2d &b, const cv::Point2d &c)
        {
            return cross(b - a, c - a);
        }

        /**
         * @brief 判断线段是否严格相交。
         *
         * 用于检查：
         *
         * e0 与 e2
         *
         * e1 与 e3
         *
         * 是否形成自交。
         */
        bool segments_intersect(const cv::Point2d &a, const cv::Point2d &b, const cv::Point2d &c,
                                const cv::Point2d &d)
        {
            const double ab_c = orientation(a, b, c);

            const double ab_d = orientation(a, b, d);

            const double cd_a = orientation(c, d, a);

            const double cd_b = orientation(c, d, b);

            return ab_c * ab_d < 0.0 && cd_a * cd_b < 0.0;
        }

        /**
         * @brief 检查四个点是否形成凸四边形。
         *
         * 输入顺序固定：
         *
         * LT -> RT -> RB -> LB
         *
         * 计算相邻边叉积。
         *
         * 凸四边形：
         *
         * 所有叉积符号一致。
         */
        // 注：四点完全共线时叉积全 0，本函数返回 true（视为退化情况，实际检测器不会产生）。
        bool is_convex(const std::array<cv::Point2d, 4> &points)
        {
            std::array<double, 4> turns;

            for (int i = 0; i < 4; ++i)
            {
                const cv::Point2d current = points[i];

                const cv::Point2d next = points[(i + 1) % 4];

                const cv::Point2d next_next = points[(i + 2) % 4];

                turns[i] = orientation(current, next, next_next);
            }
            /*  现在把 0 当中性（不置 positive/negative），[+,0,+,+] 会返回 true,被判为凸。但伪代码要求"全部 >0 或全部 <0"，0 应该判为非凸。
                bool positive = false;
                bool negative = false;

                for (double value :
                     turns)
                {
                    if (value > 0.0)
                    {
                        positive = true;
                    }

                    if (value < 0.0)
                    {
                        negative = true;
                    }
                }


                 * 同时存在正负方向，
                 * 说明存在凹角或点序异常。

                return !(positive && negative);
            */
            // 严格凸要求：四个转弯叉积全 >0 或全 <0。
            // 若出现 0（共线），视为退化，不算凸。
            // 现在is_convex 的逻辑：[+,0,+,+] → all_positive=false（因为 0 ≤ 0），all_negative=false → 返回 false → 拒绝。
            bool all_positive = true;
            bool all_negative = true;

            for (double value : turns)
            {
                if (value <= 0.0)
                {
                    all_positive = false;
                }

                if (value >= 0.0)
                {
                    all_negative = false;
                }
            }

            return all_positive || all_negative;
        }

        /**
         * @brief 检查 physical_to_screen 映射是否合法。
         *
         * 后续重建屏幕顺序前必须先检查。
         *
         * 防止：
         *
         * - 越界访问；
         * - 两个物理角占同一屏幕位置。
         */
        bool validate_mapping(const ScreenOrder &order)
        {
            std::array<bool, 4> used = {false, false, false, false};

            for (int i = 0; i < 4; ++i)
            {
                const int screen_index = order.physical_to_screen_[i];

                if (screen_index < 0 || screen_index >= 4)
                {
                    return false;
                }

                if (used[screen_index])
                {
                    return false;
                }

                used[screen_index] = true;
            }

            return true;
        }

        /**
         * @brief 根据 physical_to_screen_ 恢复屏幕顺序点。
         *
         * 输入：
         *
         * P0~P3 物理点
         *
         * 输出：
         *
         * LT RT RB LB
         *
         * 不重新判断哪个点是什么。
         */
        std::array<cv::Point2d, 4> reorder_points(const CornerMeasurement &measurement,
                                                  const ScreenOrder &order)
        {
            std::array<cv::Point2d, 4> result;

            for (int physical_index = 0; physical_index < 4; ++physical_index)
            {
                const int screen_index = order.physical_to_screen_[physical_index];

                result[screen_index] = measurement.physical_corners_[physical_index];
            }

            return result;
        }

        /**
         * @brief 检查点是否在原图范围内。
         */
        bool inside_image(const cv::Point2d &point, cv::Size size)
        {
            return point.x >= 0.0 && point.x < size.width && point.y >= 0.0 &&
                   point.y < size.height;
        }

    } // namespace

    DetectionValidation validateDetectionGeometry(const CornerMeasurement &measurement,
                                                  const ScreenOrder &order, cv::Size original_size,
                                                  const CornerConfig &config)
    {
        // 最终发布前复查证据，不能假定上游已保证而只检查凸四点。

        DetectionValidation result;

        result.valid_ = false;
        result.rejection_reason_.clear();

        /*
         * Step 1:
         * 检查物理角点是否有限。
         */
        for (const auto &point : measurement.physical_corners_)
        {
            if (!std::isfinite(point.x) || !std::isfinite(point.y))
            {
                result.rejection_reason_ = "角点存在非有限值";

                return result;
            }
        }

        /*
         * Step 2:
         * 先验证映射。
         *
         * 因为下一步需要使用：
         *
         * physical_to_screen_
         *
         * 重建屏幕顺序。
         */
        if (!validate_mapping(order))
        {
            result.rejection_reason_ = "physical_to_screen映射非法";

            return result;
        }

        /*
         * Step 3:
         * 根据已经确认合法的映射恢复：
         *
         * LT RT RB LB
         */
        const auto points = reorder_points(measurement, order);

        /*
         * Step 4:
         * 检查边是否退化。
         */
        for (int i = 0; i < 4; ++i)
        {
            const cv::Point2d edge = points[(i + 1) % 4] - points[i];

            const double length = std::sqrt(edge.x * edge.x + edge.y * edge.y);

            if (length <= 0.0)
            {
                result.rejection_reason_ = "检测四边形存在退化边";

                return result;
            }
        }

        /*
         * Step 5:
         * 检查凸性。
         */
        if (!is_convex(points))
        {
            result.rejection_reason_ = "检测四边形不是凸四边形";

            return result;
        }

        /*
         * Step 6:
         * 检查自交。
         *
         * 只需要检查非相邻边：
         *
         * e0/e2
         * e1/e3
         */
        if (segments_intersect(points[0], points[1], points[2], points[3]) ||
            segments_intersect(points[1], points[2], points[3], points[0]))
        {
            result.rejection_reason_ = "检测四边形自交";

            return result;
        }

        /*
         * Step 7:
         * 检查是否越过原图边界。
         */
        for (const auto &point : points)
        {
            if (!inside_image(point, original_size))
            {
                result.rejection_reason_ = "角点超出原图范围";

                return result;
            }
        }

        if (original_size.width <= 0 || original_size.height <= 0)
        {
            result.rejection_reason_ = "INVALID_ORIGINAL_SIZE";
            return result;
        }
        // 重建点与传入屏幕点必须一致，避免混用工作图/原图坐标域。
        for (int i = 0; i < 4; ++i)
        {
            if (points[i] != order.screen_points_[i])
            {
                result.rejection_reason_ = "SCREEN_MAPPING_MISMATCH";
                return result;
            }
        }
        if (!config.observation_budget_)
        {
            result.rejection_reason_ = "CORNER_BUDGET_NOT_CONFIGURED";
            return result;
        }
        validateCornerObservationBudget(config);
        std::set<size_t> source_components;
        for (int i = 0; i < 4; ++i)
        {
            const auto &e = measurement.evidence_[i];
            // 证据本身也必须处于同一原图域；画内四点不能掩盖画外支持段。
            for (const auto &arc : e.original_support_arcs_)
                for (auto p : arc)
                {
                    if (!std::isfinite(p.x) || !std::isfinite(p.y) ||
                        !inside_image(p, original_size))
                    {
                        result.rejection_reason_ = "EVIDENCE_OUTSIDE_ORIGINAL";
                        return result;
                    }
                }
            for (auto p : e.original_turn_arc_)
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || !inside_image(p, original_size))
                {
                    result.rejection_reason_ = "TURN_OUTSIDE_ORIGINAL";
                    return result;
                }
            if (e.frame_id_ != measurement.evidence_[0].frame_id_ ||
                !source_components.insert(e.component_id_).second)
            {
                result.rejection_reason_ = "EVIDENCE_SOURCE_CONFLICT";
                return result;
            }
            if (!validateCornerEvidence(e, measurement.physical_corners_[i], i, config,
                                        result.rejection_reason_))
                return result;
        }

        /*
         * Step 8:
         * 全部通过。
         */
        result.valid_ = true;

        result.rejection_reason_.clear();

        return result;
    }

} // namespace mark
