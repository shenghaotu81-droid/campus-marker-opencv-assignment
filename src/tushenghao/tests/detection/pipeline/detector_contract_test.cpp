// Detector 类的（测 detector.cpp）：机器本身是好的（能造、config 对、process 能调、reset 不崩、两个实例互不干扰）
/* 验证 Detector 这个"机器"本身是好的：
1. 给合法配置 → 能造出来；给非法配置 → 抛 ConfigError
2. config() 返回的和当初给的一致
3. process() 给一帧 → 不崩溃，能返回结果
4. reset() 随便调 → 不崩溃
5. 两个 Detector 各玩各的，互不干扰
6. 不验证算法对不对（那是板块2），只验证"机器能开机、按钮都能按"。
*/
#include "vision/detection/detector.hpp"

#include <iostream>
#include <filesystem>

#include <opencv2/opencv.hpp>

namespace
{

    // 创建合法 DetectorConfig，用于验证 Detector 接口调用链。
    mark::DetectorConfig createConfig()
    {
        mark::DetectorConfig config;

        config.schema_version = 1;

        config.input.pixel_format = "BGR8";
        config.input.timestamp_unit = "us";

        config.preprocess.work_width = 960;
        config.preprocess.work_height = 720;
        config.preprocess.threshold = 200;

        config.mode = mark::DetectorMode::SegmentedMark;



        config.debug.timing_enabled = false;

        return config;
    }

    // 测试 Detector 构造和 config() 接口，期望返回保存的原始配置。
    bool testConstructAndConfig()
    {
        mark::DetectorConfig config = createConfig();

        mark::Detector detector(config);

        const auto &stored = detector.config();

        return stored.schema_version == config.schema_version &&
               stored.input.pixel_format == config.input.pixel_format &&
               stored.input.timestamp_unit == config.input.timestamp_unit &&
               stored.preprocess.work_width ==
                   config.preprocess.work_width &&
               stored.preprocess.work_height ==
                   config.preprocess.work_height &&
               stored.preprocess.threshold ==
                   config.preprocess.threshold &&
               stored.mode == config.mode;
    }

    // 原测试丢弃结果，无法发现绕过稳定层；现在检查状态、元数据和空载荷。
    bool testProcess()
    {
        mark::Detector detector(createConfig());

        mark::FrameInput frame{};

        // Step 8.1 后 process() 跑真 pipeline，需要非空输入图。
        frame.image = cv::Mat(720, 960, CV_8UC3, cv::Scalar(0, 0, 0));
        frame.frame_id = 42;
        frame.timestamp_us = 14000;

        mark::FrameResult result =
            detector.process(frame);

        return result.status == mark::Status::NOT_READY &&
               result.frame_id == frame.frame_id &&
               result.timestamp_us == frame.timestamp_us &&
               result.detections.empty();
    }

    // 测试 reset 接口可以正常调用，期望不会崩溃。
    bool testReset()
    {
        mark::Detector detector(createConfig());

        detector.reset(mark::ResetReason::External);

        return true;
    }

    // 测试多个 Detector 实例相互独立，期望一个实例 reset 不影响另一个实例。
    bool testMultipleInstances()
    {
        mark::DetectorConfig config = createConfig();

        mark::Detector detector_a(config);
        mark::Detector detector_b(config);

        detector_a.reset(mark::ResetReason::External);

        return detector_a.config().schema_version ==
                   detector_b.config().schema_version &&
               detector_a.config().mode ==
                   detector_b.config().mode;
    }

} // namespace

// 主函数，运行所有测试用例，捕获异常并报告错误。
int main()
{
    try
    {
        if (!testConstructAndConfig())
        {
            std::cerr
                << "testConstructAndConfig failed"
                << std::endl;

            return 1;
        }

        if (!testProcess())
        {
            std::cerr
                << "testProcess failed"
                << std::endl;

            return 1;
        }

        if (!testReset())
        {
            std::cerr
                << "testReset failed"
                << std::endl;

            return 1;
        }

        if (!testMultipleInstances())
        {
            std::cerr
                << "testMultipleInstances failed"
                << std::endl;

            return 1;
        }

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr
            << "[unexpected] "
            << e.what()
            << std::endl;

        return 1;
    }
}
