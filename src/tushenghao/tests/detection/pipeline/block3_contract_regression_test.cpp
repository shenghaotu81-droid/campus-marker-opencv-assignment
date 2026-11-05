// 输入：固定非法输入/排序样例；输出：逐用例检查结果，任何断言失败返回非零。
// 不使用测试框架，也不依赖生产定位预算；这些测试只验证冻结的安全契约。
#include "detector/config.hpp"
#include "vision/detection/detector.hpp"
#include "preprocess/preprocess.hpp"
#include "block3_fixture.hpp"
#include "corner_evidence_check.hpp"
#include "corners/measurement/corner_resolver.hpp"
#include "corners/validation/detection_validator.hpp"
#include "corners/publication/detection_publication.hpp"
#include "vision/types/detection/corner_types.hpp"
#include "reader/config_error.hpp"
#include "vision/math/observed_geometry.hpp"
#include <fstream>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    // Release 下仍执行检查，避免 assert 在 NDEBUG 时失效。
    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    mark::DetectorConfig config()
    {
        return mark::loadConfig(std::filesystem::path(VISION_SOURCE_ROOT) /
                                "config/detector.yaml")
            .detector_config;
    }

    // 明确时间来源的当前唯一合法枚举，避免未初始化源值进入公共入口。
    mark::FrameInput frame(uint64_t id, int64_t time)
    {
        mark::FrameInput input{};
        input.image = cv::Mat(1080, 1440, CV_8UC3, cv::Scalar(0, 0, 0));
        input.frame_id = id;
        input.timestamp_us = time;
        input.time_source = mark::TimestampSource::Unknown;
        return input;
    }

    // 原入口将非法图像送进 cvtColor 抛异常，或把空检测当 NOT_DETECTED。
    void invalidInput()
    {
        mark::Detector detector(config());
        auto input = frame(0, 0);
        input.image = cv::Mat(2, 2, CV_8UC1);
        check(detector.process(input).status == mark::Status::INVALID_INPUT, "gray input accepted");
        input.image = cv::Mat(2, 2, CV_32FC3);
        check(detector.process(input).status == mark::Status::INVALID_INPUT,
              "float input accepted");
        input.image.release();
        check(detector.process(input).status == mark::Status::INVALID_INPUT,
              "empty input accepted");
        input = frame(0, -1);
        check(detector.process(input).status == mark::Status::INVALID_INPUT,
              "negative time accepted");
        input = frame(0, 0);
        input.time_source = static_cast<mark::TimestampSource>(123);
        check(detector.process(input).status == mark::Status::INVALID_INPUT,
              "unknown enum value accepted");
    }

    // 完整获批配置黑帧正常NOT_DETECTED；非法后低id恢复/reset/实例隔离仍保持负例。
    void sequence()
    {
        mark::Detector a(config()), b(config());
        check(a.process(frame(5, 100)).status == mark::Status::NOT_DETECTED,
              "ready black frame status incorrect");
        check(a.process(frame(5, 101)).status == mark::Status::INVALID_INPUT,
              "duplicate id accepted");
        a.reset(mark::ResetReason::InvalidSequence);
        check(a.process(frame(1, 50)).status == mark::Status::NOT_DETECTED,
              "reset did not clear sequence");
        check(a.process(frame(2, 49)).status == mark::Status::INVALID_INPUT,
              "backwards time accepted");
        check(b.process(frame(0, 0)).status == mark::Status::NOT_DETECTED, "instance state leaked");
    }

    // NaN/Inf 不能进入归一化；失败必须没有 payload 并携带原因。
    void nonFinite()
    {
        for (double value :
             {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
        {
            std::array<cv::Point2d, 4> points{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
            points[0].x = value;
            auto result = mark::orderScreenCorners(points, {});
            check(result.status_ == mark::ScreenOrderStatus::FAILED && !result.screen_order_ &&
                      !result.rejection_reason_.empty(),
                  "nonfinite sorting input accepted");
        }
    }

    // 零语义门限表示精确相等，不应把同一组四点判成冲突。


    // 同残差的测量容器重排后仍由稳定观测编号决定，非法残差不能参与选择。


    // 凸四点不是完整证据；错物理标签/截断/伪交点/NaN误差均须拒绝。
    void invalidEvidence()
    {
        auto s = fixture::scene();
        auto c = fixture::cornerConfig();
        auto r = fixture::resolve(s,c);
        check(bool(r.measurement_), "fixture failed");
        auto order =
            mark::orderScreenCorners(r.measurement_->physical_corners_, c).screen_order_.value();
        for (int variant = 0; variant < 13; ++variant)
        {
            auto m = *r.measurement_;
            if (variant == 0)
                m.evidence_ = {};
            if (variant == 1)
                m.evidence_[0].physical_corner_ = mark::PhysicalCorner::P1;
            if (variant == 2)
            {
                m.evidence_[0].truncated_ = true;
            }
            if (variant == 3)
                m.evidence_[0].intersection_.x += 1;
            if (variant == 4)
                m.evidence_[0].error_ = std::numeric_limits<double>::quiet_NaN();
            if (variant == 5)
                m.evidence_[0].error_ = -1;
            if (variant == 6)
                m.evidence_[0].line_a_[2] += 1;
            if (variant == 7)
                m.evidence_[0].observed_segment_ids_[1] = m.evidence_[0].observed_segment_ids_[0];
            if (variant == 8)
                m.evidence_[0].original_support_arcs_[0].erase(
                    m.evidence_[0].original_support_arcs_[0].begin() + 2);
            if (variant == 9)
                m.evidence_[0].original_turn_arc_.erase(m.evidence_[0].original_turn_arc_.begin() +
                                                        1);
            if (variant == 10)
                m.evidence_[0].original_turn_arc_.front().x += 1;
            if (variant == 11)
                m.evidence_[0].support_extension_px_[0] = std::numeric_limits<double>::infinity();
            if (variant == 12)
                m.evidence_[0].original_support_arcs_[0][1].x =
                    std::numeric_limits<double>::quiet_NaN();
            std::string reason;
            check(!mark::verifyCornerEvidence(m.evidence_[0], m.physical_corners_[0], 0, c, reason),
                  "test evidence reconstruction missed corruption");
        }
        order.screen_points_[0].x += 1;
        check(!mark::validateDetectionGeometry(*r.measurement_, order,
                                               s.frame.original_image_.size(), c)
                   .valid_,
              "screen mapping mismatch ignored");
    }

    // 原图浅视图与像素中心映射必须保持，resize只修改独立工作图。
    void preprocessMapping()
    {
        auto input = frame(4, 15);
        auto before = input.image.clone();
        auto c = config().preprocess;
        c.work_width = 960;
        c.work_height = 360;
        auto result = mark::preprocess(input, c);
        check(result.original_image_.data == input.image.data &&
                  result.original_white_threshold_ == c.threshold,
              "original channel not filled");
        auto p = cv::Point2d(777.25, 333.5);
        check(cv::norm(result.workToOriginal(result.originalToWork(p)) - p) < 1e-10,
              "pixel center mapping mismatch");
        result.image_.setTo(cv::Scalar(10, 20, 30));
        check(cv::norm(before, input.image, cv::NORM_INF) == 0,
              "preprocess polluted caller pixels");
    }

    // 所有corner浮点字段逐个检查NaN/Inf，新增显式预算导出往返不得丢字段。
    void budgetConfig()
    {
        double mark::CornerConfig::*fields[] = {
                                                &mark::CornerConfig::max_line_fit_error_,
                                                &mark::CornerConfig::max_corner_error_,
                                                &mark::CornerConfig::min_intersection_angle_deg_,
                                                &mark::CornerConfig::approximation_epsilon_,
                                                &mark::CornerConfig::max_pair_disagreement_px2_};
        for (auto field : fields)
            for (double value : {std::numeric_limits<double>::quiet_NaN(),
                                 std::numeric_limits<double>::infinity()})
            {
                mark::AppConfig app{};
                app.detector_config = config();
                app.detector_config.corner_.*field = value;
                bool threw = false;
                try
                {
                    mark::validateConfig(app);
                }
                catch (const mark::ConfigError &)
                {
                    threw = true;
                }
                check(threw, "nonfinite corner budget accepted");
            }
        // 新增optional预算也逐字段finite检查，不能只靠旧corner字段或导出成功代表安全。
        double mark::CornerObservationBudget::*observation_fields[] = {
            &mark::CornerObservationBudget::max_edge_direction_diff_deg,
            &mark::CornerObservationBudget::max_edge_position_distance_px,
            &mark::CornerObservationBudget::turn_trim_distance_px,
            &mark::CornerObservationBudget::max_turn_connection_length_px,
            &mark::CornerObservationBudget::max_support_extension_px,
            &mark::CornerObservationBudget::min_support_span_px};
        for (double value :
             {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
        {
            for (auto field : observation_fields)
            {
                mark::AppConfig app{};
                app.detector_config = config();
                app.detector_config.corner_.observation_budget_ =
                    fixture::cornerConfig().observation_budget_;
                app.detector_config.corner_.observation_budget_.value().*field = value;
                bool threw = false;
                try
                {
                    mark::validateConfig(app);
                }
                catch (const mark::ConfigError &)
                {
                    threw = true;
                }
                check(threw, "nonfinite observation budget accepted");
            }
        }
        auto path =
            std::filesystem::temp_directory_path() /
            ("block3-budget-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".yaml");
        mark::AppConfig app{};
        app.detector_config = config();
        app.detector_config.corner_ = fixture::cornerConfig();
        mark::writeEffectiveConfig(app, path);
        auto restored = mark::loadConfig(path).detector_config;
        check(restored.corner_.observation_budget_->max_support_extension_px == 6,
              "explicit budget lost on roundtrip");
        std::ifstream source(path);
        std::string text((std::istreambuf_iterator<char>(source)), {});
        source.close();
        for (const std::string suffix : {"\nunknown_budget: 1\n", "\nschema_version: 1\n", "\nmarker_geometry_path: obsolete.yaml\n"})
        {
            {
                std::ofstream out(path);
                out << text << suffix;
            }
            bool threw = false;
            try
            {
                mark::loadConfig(path);
            }
            catch (const std::exception &)
            {
                threw = true;
            }
            check(threw, "unknown/duplicate field accepted");
        }
        std::filesystem::remove(path);
    }

    // 原测试把输入预先舍入，覆盖不到生产的double→float冲突；这里直接发布真实double物理环。
    mark::CornerMeasurement rotatedMeasurement(double degrees)
    {
        mark::CornerMeasurement m{};
        m.physical_corners_ = {{{-50, -50}, {50, -50}, {50, 50}, {-50, 50}}};
        double a = degrees * CV_PI / 180, cs = std::cos(a), sn = std::sin(a);
        for (auto &p : m.physical_corners_)
            p = {150 + cs * p.x - sn * p.y, 150 + sn * p.x + cs * p.y};
        return m;
    }

    // 固定安装的小晃动：float发布不移动角点、不从输入重新推断身份。
    void publicationAngles() {
        for(double angle:{-10.,-3.,0.,3.,10.}) {
            auto m=rotatedMeasurement(angle);auto before=m.physical_corners_;
            std::string reason;auto d=mark::publishFloatDetection(m,{300,300},{},reason);
            check(bool(d),"small shake publication failed");
            check(m.physical_corners_==before,"publication moved measurements");
            for(size_t i=0;i<4;++i)check(d->corners[i]==cv::Point2f(before[i]),"float corner moved");
            check(bool(d->attributes.orientation),"orientation presence changed");
            check(*d->attributes.orientation==std::array<int,4>{{0,1,2,3}},"fixed identity lost");
            check(d->bbox==mark::observed::four_corner_bounds(d->corners),"bbox changed");
        }
    }

    // 表示舍入失败不能裁点/伪造点；覆盖原本画内却float越界和舍入后重合。
    void publicationFailures()
    {
        for (int variant = 0; variant < 7; ++variant)
        {
            auto m = rotatedMeasurement(0);
            if (variant == 0)
                m.physical_corners_ = {{{100, 100}, {100 + 1e-6, 100}, {200, 200}, {100, 200}}};
            if (variant == 1)
                m.physical_corners_[0].x = std::numeric_limits<double>::quiet_NaN();
            if (variant == 2)
                m.physical_corners_[0].y = std::numeric_limits<double>::infinity();
            if (variant == 3)
                m.physical_corners_[1].x = 300 - 1e-7;
            if (variant == 4)
                m.physical_corners_[2].y = 300 - 1e-7;
            if (variant == 5)
                m.physical_corners_[1] = m.physical_corners_[0];
            if (variant == 6)
                m.physical_corners_[2] = {150, 150};
            std::string why;
            auto d = mark::publishFloatDetection(m, {300, 300}, {}, why);
            check(!d && !why.empty(), "P03 invalid publication accepted");
        }
    }
}

// 每个用例独立捕获失败，首个失败不会隐藏后续的已知回归。
int main()
{
    const std::pair<const char *, void (*)()> cases[] = {
        {"invalid_input", invalidInput},
        {"sequence", sequence},
        {"nonfinite", nonFinite},
        {"invalid_evidence", invalidEvidence},
        {"preprocess_mapping", preprocessMapping},
        {"budget_config", budgetConfig},
        {"P01_P02_P04_float_publication", publicationAngles},
        {"P03_float_publication_rejection", publicationFailures}};
    int failed = 0;
    for (const auto &item : cases)
    {
        try
        {
            item.second();
            std::cout << "PASS " << item.first << '\n';
        }
        catch (const std::exception &error)
        {
            ++failed;
            std::cerr << "FAIL " << item.first << ": " << error.what() << '\n';
        }
    }
    return failed == 0 ? 0 : 1;
}
