// corner config fields: 23
// 配置模块的（测 config.cpp）写出再读回，14 个字段不丢（Config → YAML → Config 一致性）+9个corner相关字段
// 配置往返测试:验证"写出→读回"不丢信息：14 个字段原样回来
/* 解释必要性：
打个比方：你写一封信，寄出去再寄回来，打开一看一字不差——证明邮局没丢字、没改字。

这个测试干的就是这个：

手写一个配置（createTestConfig）：14 个字段填好值
写出去（writeEffectiveConfig）：变成 YAML 文件
读回来（loadConfig）：YAML 变回配置对象
比一比（sameConfig）：14 个字段全一样 → 过；差一个 → 挂

为啥重要：以后你调好一组参数，write 存下来，下次 load 就能复现完全相同的实验。如果 write 漏写了字段、或 load 读错了值，这个测试当场抓出来。
*/
#include "config/config.hpp"
#include "core/config_error.hpp"


#include <filesystem>
#include <iostream>

namespace
{

    // 测试 writeEffectiveConfig 写出的配置重新 load 后，14 个冻结字段保持一致。
    mark::AppConfig createTestConfig()
    {
        mark::AppConfig config;

        auto &detector = config.detector_config;

        detector.schema_version = 1;

        detector.input.pixel_format = "BGR8";
        detector.input.timestamp_unit = "us";

        detector.preprocess.work_width = 960;
        detector.preprocess.work_height = 720;
        detector.preprocess.threshold = 200;

        detector.mode = mark::DetectorMode::Skeleton;

        detector.temporal.stabilization_enabled = false;
        detector.temporal.display_hold_enabled = false;
        detector.temporal.max_hold_frames = 7;
        // 非默认fixture值验证每个新增字段不会被导出器遗漏或重载成默认。
        detector.temporal.stabilization_enabled=true;detector.temporal.display_hold_enabled=true;
        detector.temporal.reference_dt_ms=15;detector.temporal.reference_alpha=.6;
        detector.temporal.history_max_gap_ms=60;detector.temporal.max_center_distance_diagonal_ratio=.6;
        detector.temporal.min_area_ratio=.6;detector.temporal.max_area_ratio=1.8;
        detector.temporal.correspondence_uncertainty_px=2;detector.temporal.max_smoothing_deviation_px=3;

        detector.output.show_window = false;
        detector.output.show_held_state = false;

        detector.debug.timing_enabled = false;
        detector.debug.draw_candidates = false;

        detector.corner_.local_search_margin_ratio_ = 0.2;
        detector.corner_.min_line_points_ = 5;
        detector.corner_.max_line_fit_error_ = 2.0;
        detector.corner_.min_intersection_angle_deg_ = 10.0;
        detector.corner_.max_corner_error_ = 5.0;
        detector.corner_.reject_truncated_corner_ = true;
        detector.corner_.approximation_epsilon_ = 2.0;
        detector.corner_.edge_point_distance_threshold_ = 3.0;
        detector.corner_.semantic_geometry_threshold_ = 64.0; // Step 5 假设间几何一致性阈值 单位：pixel²

        return config;
    }

    // 比较 write -> load 前后的 14 个冻结字段是否完全一致。
    bool sameConfig(
        const mark::AppConfig &lhs,
        const mark::AppConfig &rhs)
    {
        const auto &a = lhs.detector_config;
        const auto &b = rhs.detector_config;

        return a.schema_version == b.schema_version &&

               a.input.pixel_format == b.input.pixel_format &&
               a.input.timestamp_unit == b.input.timestamp_unit &&

               a.preprocess.work_width == b.preprocess.work_width &&
               a.preprocess.work_height == b.preprocess.work_height &&
               a.preprocess.threshold == b.preprocess.threshold &&

               a.mode == b.mode &&

               a.temporal.stabilization_enabled ==
                   b.temporal.stabilization_enabled &&
               a.temporal.display_hold_enabled ==
                   b.temporal.display_hold_enabled &&
               a.temporal.max_hold_frames ==
                   b.temporal.max_hold_frames &&

               a.temporal.reference_dt_ms == b.temporal.reference_dt_ms &&
               a.temporal.reference_alpha == b.temporal.reference_alpha &&
               a.temporal.history_max_gap_ms == b.temporal.history_max_gap_ms &&
               a.temporal.max_center_distance_diagonal_ratio == b.temporal.max_center_distance_diagonal_ratio &&
               a.temporal.min_area_ratio == b.temporal.min_area_ratio &&
               a.temporal.max_area_ratio == b.temporal.max_area_ratio &&
               a.temporal.correspondence_uncertainty_px == b.temporal.correspondence_uncertainty_px &&
               a.temporal.max_smoothing_deviation_px == b.temporal.max_smoothing_deviation_px &&

               a.output.show_window ==
                   b.output.show_window &&
               a.output.show_held_state ==
                   b.output.show_held_state &&

               a.debug.timing_enabled ==
                   b.debug.timing_enabled &&
               a.debug.draw_candidates ==
                   b.debug.draw_candidates &&

               a.corner_.local_search_margin_ratio_ == b.corner_.local_search_margin_ratio_ &&
               a.corner_.min_line_points_ == b.corner_.min_line_points_ &&
               a.corner_.max_line_fit_error_ == b.corner_.max_line_fit_error_ &&
               a.corner_.min_intersection_angle_deg_ == b.corner_.min_intersection_angle_deg_ &&
               a.corner_.max_corner_error_ == b.corner_.max_corner_error_ &&
               a.corner_.reject_truncated_corner_ == b.corner_.reject_truncated_corner_ &&
               a.corner_.approximation_epsilon_ == b.corner_.approximation_epsilon_ &&
               a.corner_.edge_point_distance_threshold_ == b.corner_.edge_point_distance_threshold_&&
               a.corner_.semantic_geometry_threshold_ == b.corner_.semantic_geometry_threshold_;
    }

} // namespace

int main()
{
    const std::filesystem::path path =
        "roundtrip_test_config.yaml";

    try
    {
        // 写出有效配置，再重新读取，验证配置闭环。
        mark::AppConfig original = createTestConfig();

        mark::writeEffectiveConfig(
            original,
            path);

        mark::AppConfig loaded =
            mark::loadConfig(path);

        if (!sameConfig(original, loaded))
        {
            std::cerr
                << "roundtrip config mismatch"
                << std::endl;

            std::filesystem::remove(path);
            return 1;
        }

        std::filesystem::remove(path);

        return 0;
    }
    catch (const mark::ConfigError &e)
    {
        std::cout
            << "[unexpected] "
            << e.what()
            << std::endl;

        std::filesystem::remove(path);
        return 1;
    }
}