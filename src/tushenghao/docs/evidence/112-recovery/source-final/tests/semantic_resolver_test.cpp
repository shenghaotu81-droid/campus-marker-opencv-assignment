/* 6 个用例，验证block2 §4.3 的四条规则：
空输入：传空 vector，期望 geometry_consistent_=false，有拒绝原因，不崩。
单测量：只有一个假设，几何自洽，方向唯一（search_truncated=false 时）。
几何一致+方向相同：两个假设形状一样、P0 位置也一样→合并，留残差小的那个。
几何一致+方向不同：形状一样但 P0 转了 90°→geometry_consistent_=true，orientation_unique_=false。
几何冲突：两个假设形状差太多→geometry_consistent_=false，retained_measurements_ 为空。
search_truncated：即使只有一个假设、方向本该唯一，传 true 进去→orientation_unique_ 强制 false。
*/
// tests/semantic_resolver_test.cpp
#include <iostream>
#include <stdexcept>
#include <utility>
#include <cmath>

// 普通C++检查在Release仍有效；每用例失败由main独立记录，不引入测试框架。
namespace
{
    void check(bool ok, const char *message)
    {
        if (!ok)
            throw std::runtime_error(message);
    }
}

#include "corners/semantic_resolver.hpp"
#include "mark/detector_config.hpp"

namespace mark
{

    namespace
    {

        /**
         * @brief 构造一个基础四角测量结果。
         *
         * 用固定矩形作为测试几何。
         *
         * 测试重点：
         * - 不测试视觉检测；
         * - 只测试 Step 5 语义归并。
         */
        CornerMeasurement make_measurement(double offset_x, double offset_y, double error)
        {
            CornerMeasurement measurement;

            /*
             * physical_corners_ 固定表示 P0~P3。
             *
             * 这里故意使用 cv::Point2d 显式构造，
             * 避免 initializer_list 类型推导问题。
             */
            measurement.physical_corners_ = {cv::Point2d{offset_x + 0.0, offset_y + 0.0},
                                             cv::Point2d{offset_x + 10.0, offset_y + 0.0},
                                             cv::Point2d{offset_x + 10.0, offset_y + 10.0},
                                             cv::Point2d{offset_x + 0.0, offset_y + 10.0}};

            /*
             * 四个角证据都手动填残差。
             *
             * Step 5 只读取：
             *
             * CornerEvidence.error_
             *
             * 选择残差最小的已有 measurement。
             */
            for (auto &evidence : measurement.evidence_)
            {
                evidence.error_ = error;
            }

            return measurement;
        }

        /**
         * @brief 构造方向不同但几何相同的 measurement。
         *
         * 保留同一个四点集合，
         * 只改变 physical_corners_ 中 P0~P3 对应关系。
         *
         * 用于验证：
         *
         * 几何一致 != 方向唯一。
         */
        CornerMeasurement make_reversed_measurement(double error)
        {
            CornerMeasurement measurement;

            measurement.physical_corners_ = {cv::Point2d{10.0, 10.0}, cv::Point2d{0.0, 10.0},
                                             cv::Point2d{0.0, 0.0}, cv::Point2d{10.0, 0.0}};

            for (auto &evidence : measurement.evidence_)
            {
                evidence.error_ = error;
            }

            return measurement;
        }

        /**
         * @brief 创建测试配置。
         */
        CornerConfig make_test_config()
        {
            CornerConfig config;

            // 四点平方距离阈值。
            config.semantic_geometry_threshold_ = 100.0;

            return config;
        }

    } // namespace

    void EmptyInputFails()
    {
        auto result = resolveSemantics({}, false, make_test_config());

        check(!(result.geometry_consistent_), "FALSE check at semantic_resolver:128");

        check(!(result.orientation_unique_), "FALSE check at semantic_resolver:130");

        check(bool(result.retained_measurements_.empty()), "TRUE check at semantic_resolver:132");

        check(!(result.rejection_reason_.empty()), "FALSE check at semantic_resolver:134");
    }

    void SingleMeasurementIsUnique()
    {
        auto measurement = make_measurement(0.0, 0.0, 1.0);

        auto result = resolveSemantics({measurement}, false, make_test_config());

        check(bool(result.geometry_consistent_), "TRUE check at semantic_resolver:151");

        check(bool(result.orientation_unique_), "TRUE check at semantic_resolver:153");

        check((result.retained_measurements_.size()) == (1), "EQ check at semantic_resolver:155");

        check(bool(result.rejection_reason_.empty()), "TRUE check at semantic_resolver:157");
    }

    void SameGeometrySameOrientationKeepsLowerResidual()
    {
        auto high_error = make_measurement(0.0, 0.0, 10.0);

        auto low_error = make_measurement(0.0, 0.0, 1.0);

        auto result = resolveSemantics({high_error, low_error}, false, make_test_config());

        check(bool(result.geometry_consistent_), "TRUE check at semantic_resolver:181");

        check(bool(result.orientation_unique_), "TRUE check at semantic_resolver:183");

        check((result.retained_measurements_.size()) == (1), "EQ check at semantic_resolver:185");

        /*
         * 保留已有 measurement，
         * 不平均生成新点。
         *
         * 这里检查残差更小的那个。
         */
        check((result.retained_measurements_[0].evidence_[0].error_) == (1.0),
              "DOUBLE_EQ check at semantic_resolver:193");
    }

    /* 测试设计点：
    SameGeometryDifferentOrientationIsAmbiguous 依赖 orientation_equal() 当前实现：
    • 先找最小循环对应；
    • 再检查 best_mapping[i] == i。
    所以这个测试专门覆盖：
    同一几何
    +
    物理映射不同
    =
    geometry_consistent_=true
    orientation_unique_=false
    */
    void SameGeometryDifferentOrientationIsAmbiguous()
    {
        auto first = make_measurement(0.0, 0.0, 1.0);

        auto second = make_reversed_measurement(2.0);

        auto result = resolveSemantics({first, second}, false, make_test_config());

        check(bool(result.geometry_consistent_), "TRUE check at semantic_resolver:229");

        /*
         * 几何一致，
         * 但是物理角方向解释不同。
         */
        check(!(result.orientation_unique_), "FALSE check at semantic_resolver:235");

        check((result.retained_measurements_.size()) == (1), "EQ check at semantic_resolver:237");
    }

    void GeometryConflictFails()
    {
        auto first = make_measurement(0.0, 0.0, 1.0);

        /*
         * 平移距离远超过阈值，
         * 两个 measurement 不能解释为同一几何对象。
         */
        auto second = make_measurement(100.0, 100.0, 1.0);

        auto result = resolveSemantics({first, second}, false, make_test_config());

        check(!(result.geometry_consistent_), "FALSE check at semantic_resolver:265");

        check(bool(result.retained_measurements_.empty()), "TRUE check at semantic_resolver:267");

        check(!(result.rejection_reason_.empty()), "FALSE check at semantic_resolver:269");
    }

    void SearchTruncatedForcesOrientationFalse()
    {
        auto measurement = make_measurement(0.0, 0.0, 1.0);

        auto result = resolveSemantics({measurement}, true, make_test_config());

        check(bool(result.geometry_consistent_), "TRUE check at semantic_resolver:286");

        /*
         * 即使当前只有一个方向解释，
         * 搜索截断也不能声明方向唯一。
         */
        check(!(result.orientation_unique_), "FALSE check at semantic_resolver:292");

        check((result.retained_measurements_.size()) == (1), "EQ check at semantic_resolver:294");
    }

} // namespace mark

// 每个历史用例仍独立执行，失败不会跳过其它用例。
int main()
{
    int failures = 0;
    const std::pair<const char *, void (*)()> cases[] = {
        {"EmptyInputFails", mark::EmptyInputFails},
        {"SingleMeasurementIsUnique", mark::SingleMeasurementIsUnique},
        {"SameGeometrySameOrientationKeepsLowerResidual",
         mark::SameGeometrySameOrientationKeepsLowerResidual},
        {"SameGeometryDifferentOrientationIsAmbiguous",
         mark::SameGeometryDifferentOrientationIsAmbiguous},
        {"GeometryConflictFails", mark::GeometryConflictFails},
        {"SearchTruncatedForcesOrientationFalse", mark::SearchTruncatedForcesOrientationFalse},
    };
    for (auto item : cases)
    {
        try
        {
            item.second();
            std::cout << "PASS " << item.first << "\n";
        }
        catch (const std::exception &e)
        {
            ++failures;
            std::cerr << "FAIL " << item.first << ": " << e.what() << "\n";
        }
    }
    return failures ? 1 : 0;
}
