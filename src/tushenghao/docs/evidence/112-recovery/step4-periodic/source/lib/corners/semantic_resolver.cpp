#include "corners/semantic_resolver.hpp"
#include "mark/detector_config.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>
#include <stdexcept>
#include <tuple>

namespace mark
{

    namespace
    {

        // part1:4 个工具函数
        // 算两组四个点的"距离"——每个点对的平方距离加起来。值越小，两组点越像。
        /**
         * @brief 计算两个四点集合的平方距离。
         *
         * 公式：
         *
         * D = Σ ||pi - qi||²
         *
         * i = 0..3
         *
         * 用于 Step 5 几何一致性判断。
         *
         * 注意：
         *
         * 这里不认为：
         *
         * P0 对 P0
         * P1 对 P1
         *
         * 因为语义归并阶段比较的是：
         *
         * "两个假设是不是同一个几何对象"
         *
         * 而不是重新解释物理身份。
         */
        double point_set_distance(const std::array<cv::Point2d, 4> &a,
                                  const std::array<cv::Point2d, 4> &b)
        {
            double distance = 0.0;

            for (size_t i = 0; i < 4; ++i)
            {
                const double dx = a[i].x - b[i].x;

                const double dy = a[i].y - b[i].y;

                distance += dx * dx + dy * dy;
            }

            return distance;
        }

        // 把四个点按环形重新排序。比如 [0,1,2,3] 从 2 开始就是 [2,3,0,1]，反向就是 [2,1,0,3]。因为同一个四边形，起点和方向可能不同。
        /**
         * @brief 构造四点循环对应。
         *
         * 同一个几何对象可能存在不同遍历方向：
         *
         * A:
         * 0 1 2 3
         *
         * B:
         * 2 1 0 3
         *
         * 因此需要枚举：
         *
         * - 四个循环起点；
         * - 正向；
         * - 反向。
         */
        std::array<cv::Point2d, 4> make_cyclic_order(const std::array<cv::Point2d, 4> &points,
                                                     int start, bool reverse)
        {
            std::array<cv::Point2d, 4> result;

            for (int i = 0; i < 4; ++i)
            {
                int index;

                if (!reverse)
                {
                    index = (start + i) % 4;
                }
                else
                {
                    index = (start - i + 8) % 4;
                }

                result[i] = points[index];
            }

            return result;
        }

        // 核心判断。用上面两个工具，试 8 种排法（4 个起点 × 正反向），找最小距离，小于阈值就认作"同一个几何"。
        /**
         * @brief 判断两个 CornerMeasurement 几何是否一致。
         *
         * 比较：
         *
         * 无标签四点集合。
         *
         * 方法：
         *
         * 1. 枚举循环对应；
         * 2. 计算四点平方和；
         * 3. 取最小距离；
         * 4. 与 semantic_geometry_threshold_ 比较。
         */
        bool geometry_equal(const CornerMeasurement &a, const CornerMeasurement &b,
                            double threshold)
        {
            double best_distance = std::numeric_limits<double>::max();

            for (int start = 0; start < 4; ++start)
            {
                for (bool reverse : {false, true})
                {
                    auto candidate = make_cyclic_order(b.physical_corners_, start, reverse);

                    const double distance = point_set_distance(a.physical_corners_, candidate);

                    best_distance = std::min(best_distance, distance);
                }
            }

            // 零预算表示精确相等；严格 < 会把相同四点误判为冲突。
            return best_distance <= threshold;
        }

        // 算一个假设的总残差——把 4 个角的 evidence.error_加起来。残差越小，假设越可信。
        /**
         * @brief 计算单个测量结果的观测残差。
         *
         * 不重新计算视觉误差。
         *
         * 直接使用 Step 3 已生成的：
         *
         * CornerEvidence.error_
         *
         * 原因：
         *
         * Step 5 只负责语义选择，
         * 不重新评价图像证据。
         */
        double measurement_residual(const CornerMeasurement &measurement)
        {
            double residual = 0.0;

            for (const auto &evidence : measurement.evidence_)
            {
                residual += evidence.error_;
            }

            return residual;
        }

        // Part 2:2 个工具 + 1 个主函数
        // 判方向是否一致。先找到两组点空间上最贴合的对齐方式，再看 P0 是不是对 P0、P1 对 P1……如果 P0 对上了 P3，说明几何一样但转了个方向，返回 false。
        /**
         * @brief 判断两个测量是否拥有相同方向解释。
         *
         * 前提：
         *
         * 两个 measurement 已经通过 geometry_equal()
         * 判断为同一个几何对象。
         *
         * 这里继续判断：
         *
         * 同一个物理角是否对应同一个几何位置。
         *
         * 如果：
         *
         * 几何一致，但是物理身份映射不同，
         *
         * 说明存在方向歧义。
         */
        bool orientation_equal(const CornerMeasurement &a, const CornerMeasurement &b)
        {
            double best_distance = std::numeric_limits<double>::max();

            std::array<int, 4> best_mapping = {-1, -1, -1, -1};

            /*
             * 找到两个无标签点集之间
             * 距离最小的循环对应。
             */
            for (int start = 0; start < 4; ++start)
            {
                for (bool reverse : {false, true})
                {
                    double distance = 0.0;

                    std::array<int, 4> mapping;

                    for (int i = 0; i < 4; ++i)
                    {
                        int index;

                        if (!reverse)
                        {
                            index = (start + i) % 4;
                        }
                        else
                        {
                            index = (start - i + 8) % 4;
                        }

                        mapping[i] = index;

                        const double dx = a.physical_corners_[i].x - b.physical_corners_[index].x;

                        const double dy = a.physical_corners_[i].y - b.physical_corners_[index].y;

                        distance += dx * dx + dy * dy;
                    }

                    if (distance < best_distance)
                    {
                        best_distance = distance;

                        best_mapping = mapping;
                    }
                }
            }

            /*
             * best_mapping[i]:
             *
             * 表示：
             *
             * A 中第 i 个物理角
             *
             * 对应
             *
             * B 中的位置。
             *
             * 如果 P0~P3 对应关系保持，
             * 方向解释一致。
             */
            for (int i = 0; i < 4; ++i)
            {
                if (best_mapping[i] != i)
                {
                    return false;
                }
            }

            return true;
        }

        // 从一堆几何一致的假设里挑个最好的——算每个的总残差（4 个角的 error_ 相加），谁小留谁。残差一样按稳定观测编号，不由输入顺序决定。
        /**
         * @brief 从多个一致假设中选择最佳测量。
         *
         * 不平均多个测量结果。
         *
         * 原因：
         *
         * 平均会创造新的角点，
         * 使结果失去真实观测来源。
         *
         * 选择：
         *
         * Σ CornerEvidence.error_
         *
         * 最小的已有 measurement。
         */
        CornerMeasurement
        select_best_measurement(const std::vector<CornerMeasurement> &measurements)
        {
            size_t best_index = 0;

            double best_error = measurement_residual(measurements[0]);

            for (size_t i = 1; i < measurements.size(); ++i)
            {
                const double current_error = measurement_residual(measurements[i]);

                // 两线残差和相等时，按帧/组件/弧稳定编号组成的四角键选择。
                // 不平均角点，也不把“测量择优”当作方向胜出。
                std::array<std::string, 4> current_ids, best_ids;
                for (size_t corner = 0; corner < 4; ++corner)
                {
                    current_ids[corner] = measurements[i].evidence_[corner].stable_id_;
                    best_ids[corner] = measurements[best_index].evidence_[corner].stable_id_;
                }
                if (current_error < best_error ||
                    (current_error == best_error && current_ids < best_ids))
                {
                    best_index = i;

                    best_error = current_error;
                }
            }

            return measurements[best_index];
        }

    } // namespace

    // 主函数:分组→选最优→判方向
    /*四步走：
    空输入→直接拒绝
    两两比几何，有一对对不上→"存在几何冲突"，返回
    几何都一致→选残差最小的留下来
    两两比方向，有不一样→orientation_unique_=false；最后如果 search_truncated 为 true，再强制压成 false
    */
    /**
     * @brief 语义归并多个角点测量。
     *
     * Step 5 职责：
     *
     * 1. 判断多个 CornerMeasurement 是否几何一致；
     * 2. 判断方向是否唯一；
     * 3. 保留最佳已有假设。
     *
     * 不负责：
     *
     * - 图像检测；
     * - 边拟合；
     * - 屏幕排序。
     */
    SemanticResolution resolveSemantics(const std::vector<CornerMeasurement> &measurements,
                                        bool search_truncated, const CornerConfig &config)
    {
        SemanticResolution result;

        result.geometry_consistent_ = false;

        result.orientation_unique_ = false;

        result.retained_measurements_.clear();

        result.rejection_reason_.clear();

        /*
         * Step 0:
         * 空输入没有语义结果。
         */
        if (measurements.empty())
        {
            result.rejection_reason_ = "没有可归并的几何测量";

            return result;
        }

        // 非有限配置是调用错误，不能吞成“没有目标”。测量非法则明确拒绝。
        if (!std::isfinite(config.semantic_geometry_threshold_) ||
            config.semantic_geometry_threshold_ < 0)
            throw std::invalid_argument("SEMANTIC_CONFIG: invalid geometry threshold");
        for (const auto &measurement : measurements)
        {
            auto order = orderScreenCorners(measurement.physical_corners_, config);
            if (order.status_ != ScreenOrderStatus::SUCCESS ||
                !std::isfinite(measurement_residual(measurement)))
            {
                result.rejection_reason_ = "INVALID_MEASUREMENT: 非有限或非法四角/残差";
                return result;
            }
            for (const auto &evidence : measurement.evidence_)
            {
                if (!std::isfinite(evidence.error_) || evidence.error_ < 0)
                {
                    result.rejection_reason_ = "INVALID_MEASUREMENT: 非法观测残差";
                    return result;
                }
            }
        }

        /*
         * Step 1:
         * 几何一致性检查。
         *
         * 任意两个假设必须能够解释为
         * 同一个无标签四点集合。
         */
        for (size_t i = 0; i < measurements.size(); ++i)
        {
            for (size_t j = i + 1; j < measurements.size(); ++j)
            {
                if (!geometry_equal(measurements[i], measurements[j],
                                    config.semantic_geometry_threshold_))
                {
                    result.rejection_reason_ = "存在几何冲突";

                    return result;
                }
            }
        }

        result.geometry_consistent_ = true;

        /*
         * Step 2:
         * 保留最佳测量。
         *
         * 几何一致：
         * 不代表所有观测质量一样。
         *
         * 使用已有 evidence.error_
         * 选择残差最小者。
         */
        result.retained_measurements_.push_back(select_best_measurement(measurements));

        /*
         * Step 3:
         * 判断方向唯一性。
         *
         * 几何相同但物理映射不同：
         *
         * => 方向不唯一。
         */
        bool orientation_unique = true;

        for (size_t i = 1; i < measurements.size(); ++i)
        {
            if (!orientation_equal(measurements[0], measurements[i]))
            {
                orientation_unique = false;

                break;
            }
        }

        result.orientation_unique_ = orientation_unique;

        /*
         * Step 4:
         * 搜索截断覆盖。
         *
         * 即使当前没有发现冲突，
         * 也不能因为搜索不完整而声明方向唯一。
         */
        if (search_truncated)
        {
            result.orientation_unique_ = false;
        }

        return result;
    }

} // namespace mark
