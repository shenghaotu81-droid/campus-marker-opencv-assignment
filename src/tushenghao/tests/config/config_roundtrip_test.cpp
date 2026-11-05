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
#include "detector/config.hpp"
#include "reader/config_error.hpp"


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

        detector.mode = mark::DetectorMode::SegmentedMark;

        // 非默认fixture值验证每个新增字段不会被导出器遗漏或重载成默认。


        detector.debug.timing_enabled = false;

        detector.corner_.physical_to_screen_ = {{1,2,3,0}};
        detector.corner_.min_line_points_ = 5;
        detector.corner_.max_line_fit_error_ = 2.0;
        detector.corner_.min_intersection_angle_deg_ = 10.0;
        detector.corner_.max_corner_error_ = 5.0;
        detector.corner_.approximation_epsilon_ = 2.0;
        detector.corner_.max_pair_disagreement_px2_ = 64.0; // Step 5 假设间几何一致性阈值 单位：pixel²

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

               a.debug.timing_enabled ==
                   b.debug.timing_enabled &&

               a.corner_.physical_to_screen_ == b.corner_.physical_to_screen_ &&
               a.corner_.min_line_points_ == b.corner_.min_line_points_ &&
               a.corner_.max_line_fit_error_ == b.corner_.max_line_fit_error_ &&
               a.corner_.min_intersection_angle_deg_ == b.corner_.min_intersection_angle_deg_ &&
               a.corner_.max_corner_error_ == b.corner_.max_corner_error_ &&
               a.corner_.approximation_epsilon_ == b.corner_.approximation_epsilon_ &&
               a.corner_.max_pair_disagreement_px2_ == b.corner_.max_pair_disagreement_px2_;
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