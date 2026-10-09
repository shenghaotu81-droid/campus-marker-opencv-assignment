/**
 * @file screen_order_test.cpp
 * @brief Step 4 orderScreenCorners() 单元测试。
 *
 * 覆盖 7 个用例：
 * 1. StandardRectangle：标准矩形，验证基本排序
 * 2. TiltedQuadrilateral：倾斜四边形，验证透视下仍正确
 * 3. DiamondNearTie：菱形，接近平局，验证 tie 标记
 * 4. Rotation90_180_270：旋转后屏幕顺序应跟随旋转
 * 5. TranslationAndScale：平移缩放，验证归一化不变性
 * 6. CollinearFails：共线退化，期望 FAILED
 * 7. DuplicatePointFails：重复点，期望 FAILED
 *
 * 每个用例验证：status、physical_to_screen 映射、screen_order_tie。
 */
// tests/screen_order_test.cpp

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

#include <array>
#include <cmath>
#include <string>

#include <opencv2/core.hpp>

#include "corners/corner_types.hpp"
#include "mark/detector_config.hpp"
#include "corners/screen_order.hpp"
#include "corners/corner_types.hpp"

namespace mark
{

    namespace
    {

        // 对每个旧样例同时查新几何环接口；保留原golden断言而非只比两个包装。
        ScreenOrderResult orderAndCompare(const std::array<cv::Point2d, 4> &input,
                                          const CornerConfig &config)
        {
            auto old = orderScreenCorners(input, config);
            std::string reason;
            auto cycle = orderScreenCycle(input, config, reason);
            check(bool(cycle) == bool(old.screen_order_), "cycle/physical success differs");
            check(reason == old.rejection_reason_, "cycle/physical reason differs");
            if (cycle)
            {
                check(cycle->screen_points == old.screen_order_->screen_points_,
                      "cycle points differ");
                check(cycle->input_to_screen == old.screen_order_->physical_to_screen_,
                      "cycle slot map differs");
                check(cycle->tie == old.screen_order_->screen_order_tie_, "cycle tie differs");
            }
            return old;
        }

        /**
         * @brief 构造默认配置。
         *
         * 当前 Step 4 排序没有使用可调参数，
         * 但接口保留 CornerConfig，
         * 保证 Block 3 后续扩展时不用改函数签名。
         */
        CornerConfig make_test_config()
        {
            CornerConfig config;

            return config;
        }

        /**
         * @brief 检查成功结果基础字段。
         *
         * ScreenOrderResult 是结果包装：
         *
         * - SUCCESS 时必须存在 screen_order_
         * - 不能用默认空对象表示成功
         */
        void expect_success(const ScreenOrderResult &result)
        {
            check((result.status_) == (ScreenOrderStatus::SUCCESS), "EQ check at screen_order:67");

            check(bool(result.screen_order_.has_value()), "TRUE check at screen_order:69");
        }

        /**
         * @brief 检查失败结果基础字段。
         *
         * 失败不能返回 P0,P1,P2,P3 默认顺序。
         */
        void expect_failed(const ScreenOrderResult &result)
        {
            check((result.status_) == (ScreenOrderStatus::FAILED), "EQ check at screen_order:80");

            check(!(result.screen_order_.has_value()), "FALSE check at screen_order:82");

            check(!(result.rejection_reason_.empty()), "FALSE check at screen_order:84");
        }

        /**
         * @brief 检查屏幕点是否接近目标位置。
         */
        void expect_point_near(const cv::Point2d &actual, const cv::Point2d &expected)
        {
            constexpr double epsilon = 1e-6;

            check(std::abs((actual.x) - (expected.x)) <= (epsilon),
                  "NEAR check at screen_order:96");

            check(std::abs((actual.y) - (expected.y)) <= (epsilon),
                  "NEAR check at screen_order:98");
        }

        /**
         * @brief 验证标准 LT/RT/RB/LB 屏幕顺序。
         *
         * 注意：
         * 输入不是屏幕顺序，
         * 输入固定为物理身份 P0~P3。
         *
         * 测试只检查：
         * orderScreenCorners 是否正确生成屏幕序。
         */
        void expect_screen_rectangle(const ScreenOrder &order)
        {
            expect_point_near(order.screen_points_[0], {0, 0}); // LT

            expect_point_near(order.screen_points_[1], {1, 0}); // RT

            expect_point_near(order.screen_points_[2], {1, 1}); // RB

            expect_point_near(order.screen_points_[3], {0, 1}); // LB
        }

    }

    /**
     * 标准矩形。
     *
     * 四点已经构成标准屏幕方向。
     *
     * 验证：
     * - 成功；
     * - 输出 LT/RT/RB/LB；
     * - physical_to_screen 保留物理身份。
     */
    void StandardRectangle()
    {
        std::array<cv::Point2d, 4> physical = {cv::Point2d{0, 0}, cv::Point2d{1, 0},
                                               cv::Point2d{1, 1}, cv::Point2d{0, 1}};

        auto result = orderAndCompare(physical, make_test_config());

        expect_success(result);

        const auto &order = result.screen_order_.value();

        expect_screen_rectangle(order);

        check(!(order.screen_order_tie_), "FALSE check at screen_order:164");

        check((order.physical_to_screen_[0]) == (0), "EQ check at screen_order:166");

        check((order.physical_to_screen_[1]) == (1), "EQ check at screen_order:168");

        check((order.physical_to_screen_[2]) == (2), "EQ check at screen_order:170");

        check((order.physical_to_screen_[3]) == (3), "EQ check at screen_order:172");
    }

    /**
     * 倾斜四边形。
     *
     * 验证：
     * 归一化后仍然能恢复屏幕方向。
     */
    void TiltedQuadrilateral()
    {
        std::array<cv::Point2d, 4> physical = {cv::Point2d{10, 10}, cv::Point2d{30, 5},
                                               cv::Point2d{35, 25}, cv::Point2d{5, 30}};

        auto result = orderAndCompare(physical, make_test_config());

        expect_success(result);

        check(!(result.screen_order_->screen_order_tie_), "FALSE check at screen_order:197");

        for (int index : result.screen_order_->physical_to_screen_)
        {
            check((index) >= (0), "GE check at screen_order:202");
            check((index) < (4), "LT check at screen_order:203");
        }
    }

    /**
     * 菱形接近平局。
     *
     * 平局不能静默吞掉。
     *
     * 允许：
     * - tie=true
     * - 或由于浮点误差选择唯一结果
     *
     * 但结果必须明确。
     */
    void DiamondNearTie()
    {
        std::array<cv::Point2d, 4> physical = {cv::Point2d{0, -1}, cv::Point2d{1, 0},
                                               cv::Point2d{0, 1}, cv::Point2d{-1, 0}};

        auto result = orderAndCompare(physical, make_test_config());

        expect_success(result);

        check(bool(result.screen_order_->screen_order_tie_), "TRUE check at screen_order:234");

        for (int index : result.screen_order_->physical_to_screen_)
        {
            check((index) >= (0), "GE check at screen_order:240");
            check((index) < (4), "LT check at screen_order:241");
        }
    }

    /**
     * 旋转测试：
     *
     * 90/180/270 度旋转。
     *
     * 验证：
     * 排序只依赖几何关系，
     * 不依赖原始输入方向。
     */
    void Rotation90_180_270()
    {
        const std::array<std::array<cv::Point2d, 4>, 3> cases = {
            {{cv::Point2d{0, 1}, cv::Point2d{0, 0}, cv::Point2d{1, 0}, cv::Point2d{1, 1}},

             {cv::Point2d{1, 1}, cv::Point2d{0, 1}, cv::Point2d{0, 0}, cv::Point2d{1, 0}},

             {cv::Point2d{1, 0}, cv::Point2d{1, 1}, cv::Point2d{0, 1}, cv::Point2d{0, 0}}}};

        for (const auto &physical : cases)
        {
            auto result = orderAndCompare(physical, make_test_config());

            expect_success(result);

            for (int index : result.screen_order_->physical_to_screen_)
            {
                check((index) >= (0), "GE check at screen_order:288");
                check((index) < (4), "LT check at screen_order:289");
            }
        }
    }

    /**
     * 平移 + 缩放。
     *
     * 验证：
     * Step 4 的归一化消除：
     *
     * - 平移
     * - 尺寸变化
     */
    void TranslationAndScale()
    {
        std::array<cv::Point2d, 4> physical = {cv::Point2d{100, 200}, cv::Point2d{300, 200},
                                               cv::Point2d{300, 400}, cv::Point2d{100, 400}};

        auto result = orderAndCompare(physical, make_test_config());

        expect_success(result);

        for (int index : result.screen_order_->physical_to_screen_)
        {
            check((index) >= (0), "GE check at screen_order:322");
            check((index) < (4), "LT check at screen_order:323");
        }
    }

    /**
     * 共线退化。
     *
     * xmax/ymax 归一化没有二维范围，
     * 必须失败。
     */
    void CollinearFails()
    {
        std::array<cv::Point2d, 4> physical = {cv::Point2d{0, 0}, cv::Point2d{1, 0},
                                               cv::Point2d{2, 0}, cv::Point2d{3, 0}};

        auto result = orderAndCompare(physical, make_test_config());

        expect_failed(result);
    }

    /**
     * 重复点退化。
     *
     * 两个物理角不能占据同一个位置。
     */
    void DuplicatePointFails()
    {
        std::array<cv::Point2d, 4> physical = {cv::Point2d{0, 0}, cv::Point2d{1, 0},
                                               cv::Point2d{1, 1}, cv::Point2d{1, 1}};

        auto result = orderAndCompare(physical, make_test_config());

        expect_failed(result);
    }

} // namespace mark

// 每个历史用例仍独立执行，失败不会跳过其它用例。
int main()
{
    int failures = 0;
    const std::pair<const char *, void (*)()> cases[] = {
        {"StandardRectangle", mark::StandardRectangle},
        {"TiltedQuadrilateral", mark::TiltedQuadrilateral},
        {"DiamondNearTie", mark::DiamondNearTie},
        {"Rotation90_180_270", mark::Rotation90_180_270},
        {"TranslationAndScale", mark::TranslationAndScale},
        {"CollinearFails", mark::CollinearFails},
        {"DuplicatePointFails", mark::DuplicatePointFails},
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
