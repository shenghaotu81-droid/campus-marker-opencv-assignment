// tests/detection_validator_test.cpp
// Block3 Step 6：几何校验实现与测试:
// 8 个用例，验证 Step 6 的 7 条拒绝路径 + 1 条通过路径：合法凸四边形放行；非有限值、映射非法（越界/重复）、退化边、非凸、蝴蝶结、出界全部拒绝并给出中文原因。
/* 当前实现检查顺序：
finite
 ↓
mapping
 ↓
reorder
 ↓
退化
 ↓
凸性
 ↓
自交
*/

#include <limits>

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

#include "corners/detection_validator.hpp"
#include "mark/detector_config.hpp"
#include "block3_fixture.hpp"
#include "corners/corner_resolver.hpp"

namespace mark
{

    namespace
    {

        /**
         * @brief 构造标准合法四边形。
         *
         * physical_corners_ 固定表示：
         *
         * P0
         * P1
         * P2
         * P3
         *
         * 不表示屏幕顺序。
         */
        CornerMeasurement make_valid_measurement()
        {
            // 合法正例来自当前真实fixture取证，不能再用空证据凸四点冒充成功。
            auto scene = fixture::scene();
            auto result = resolveObservedCorners(scene.frame, scene.hypothesis, scene.geometry,
                                                 fixture::cornerConfig());
            if (!result.measurement_)
                throw std::runtime_error(result.rejection_reason_);
            return *result.measurement_;
        }

        /**
         * @brief 构造默认屏幕映射。
         *
         * P0 -> LT
         * P1 -> RT
         * P2 -> RB
         * P3 -> LB
         */
        ScreenOrder make_valid_order()
        {
            ScreenOrder order;

            order.physical_to_screen_ = {0, 1, 2, 3};

            order.screen_points_ = make_valid_measurement().physical_corners_;
            order.screen_order_tie_ = false;

            return order;
        }

        CornerConfig make_test_config()
        {
            return fixture::cornerConfig();
        }

    } // namespace

    void ValidConvexQuadrilateral()
    {
        auto result = validateDetectionGeometry(make_valid_measurement(), make_valid_order(),
                                                cv::Size(1440, 1080), make_test_config());

        check(bool(result.valid_), "TRUE check at detection_validator:105");

        check(bool(result.rejection_reason_.empty()), "TRUE check at detection_validator:107");
    }

    void NonFinitePointFails()
    {
        auto measurement = make_valid_measurement();

        measurement.physical_corners_[0] =
            cv::Point2d{std::numeric_limits<double>::quiet_NaN(), 100.0};

        auto result = validateDetectionGeometry(measurement, make_valid_order(), cv::Size(640, 480),
                                                make_test_config());

        check(!(result.valid_), "FALSE check at detection_validator:127");

        check((result.rejection_reason_.find("非有限")) != (std::string::npos),
              "NE check at detection_validator:129");
    }

    void InvalidMappingOutOfRangeFails()
    {
        auto order = make_valid_order();

        order.physical_to_screen_[0] = 5;

        auto result = validateDetectionGeometry(make_valid_measurement(), order, cv::Size(640, 480),
                                                make_test_config());

        check(!(result.valid_), "FALSE check at detection_validator:146");

        check((result.rejection_reason_.find("映射")) != (std::string::npos),
              "NE check at detection_validator:148");
    }

    void DuplicateMappingFails()
    {
        auto order = make_valid_order();

        order.physical_to_screen_[1] = 0;

        auto result = validateDetectionGeometry(make_valid_measurement(), order, cv::Size(640, 480),
                                                make_test_config());

        check(!(result.valid_), "FALSE check at detection_validator:165");

        check((result.rejection_reason_.find("映射")) != (std::string::npos),
              "NE check at detection_validator:167");
    }

    void DegenerateEdgeFails()
    {
        auto measurement = make_valid_measurement();

        /*
         * P0 与 P1 重合。
         *
         * 第一条边长度为 0。
         */
        measurement.physical_corners_[1] = measurement.physical_corners_[0];

        auto result = validateDetectionGeometry(measurement, make_valid_order(), cv::Size(640, 480),
                                                make_test_config());

        check(!(result.valid_), "FALSE check at detection_validator:190");

        check((result.rejection_reason_.find("退化")) != (std::string::npos),
              "NE check at detection_validator:192");
    }

    void ConcaveQuadrilateralFails()
    {
        auto measurement = make_valid_measurement();

        /*
         * 构造凹四边形。
         */
        measurement.physical_corners_ = {cv::Point2d{100.0, 100.0}, cv::Point2d{200.0, 100.0},
                                         cv::Point2d{150.0, 150.0}, cv::Point2d{100.0, 200.0}};

        auto result = validateDetectionGeometry(measurement, make_valid_order(), cv::Size(640, 480),
                                                make_test_config());

        check(!(result.valid_), "FALSE check at detection_validator:217");

        check((result.rejection_reason_.find("凸")) != (std::string::npos),
              "NE check at detection_validator:219");
    }

    void SelfIntersectingQuadrilateralFails()
    {
        auto measurement = make_valid_measurement();

        /*
         * 蝴蝶结结构。
         *
         * 当前实现会先在凸性检查阶段拒绝。
         */
        measurement.physical_corners_ = {cv::Point2d{100.0, 100.0}, cv::Point2d{200.0, 200.0},
                                         cv::Point2d{200.0, 100.0}, cv::Point2d{100.0, 200.0}};

        auto result = validateDetectionGeometry(measurement, make_valid_order(), cv::Size(640, 480),
                                                make_test_config());

        check(!(result.valid_), "FALSE check at detection_validator:246");

        check((result.rejection_reason_.find("凸")) != (std::string::npos),
              "NE check at detection_validator:248");
    }

    void PointOutsideImageFails()
    {
        auto measurement = make_valid_measurement();

        /*
         * 超出原图：
         *
         * width = 640
         * height = 480
         */
        measurement.physical_corners_[2] = cv::Point2d{1500.0, 900.0};

        auto result = validateDetectionGeometry(measurement, make_valid_order(),
                                                cv::Size(1440, 1080), make_test_config());

        check(!(result.valid_), "FALSE check at detection_validator:272");

        check((result.rejection_reason_.find("范围")) != (std::string::npos),
              "NE check at detection_validator:274");
    }

} // namespace mark

// 每个历史用例仍独立执行，失败不会跳过其它用例。
int main()
{
    int failures = 0;
    const std::pair<const char *, void (*)()> cases[] = {
        {"ValidConvexQuadrilateral", mark::ValidConvexQuadrilateral},
        {"NonFinitePointFails", mark::NonFinitePointFails},
        {"InvalidMappingOutOfRangeFails", mark::InvalidMappingOutOfRangeFails},
        {"DuplicateMappingFails", mark::DuplicateMappingFails},
        {"DegenerateEdgeFails", mark::DegenerateEdgeFails},
        {"ConcaveQuadrilateralFails", mark::ConcaveQuadrilateralFails},
        {"SelfIntersectingQuadrilateralFails", mark::SelfIntersectingQuadrilateralFails},
        {"PointOutsideImageFails", mark::PointOutsideImageFails},
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
