// 原沙盒计时依赖环境开关和全局插桩；这里只运行公共 process，独立记录全帧 raw 输出。
#include "mark/detector.hpp"
#include "config/config.hpp"
#include "pipeline/detector_diagnostics_access.hpp"
#include <opencv2/videoio.hpp>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv)
{
    try
    {
        if (argc != 3)
            throw std::invalid_argument("usage: recovery112_benchmark CONFIG VIDEO");
        cv::setNumThreads(1);
        const auto configuration = mark::loadConfig(argv[1]);
        mark::Detector detector(configuration.detector_config);
        cv::VideoCapture video(argv[2]);
        if (!video.isOpened())
            throw std::runtime_error("cannot open video");
        const double fps = video.get(cv::CAP_PROP_FPS);
        if (!(fps > 0))
            throw std::runtime_error("invalid fps");
        std::cout << std::setprecision(17);
        cv::Mat image;
        uint64_t frame = 0;
        while (video.read(image))
        {
            mark::FrameInput input{};
            input.image = image;
            input.frame_id = frame;
            input.timestamp_us = std::llround(frame * 1e6 / fps);
            input.time_source = mark::TimestampSource::Unknown;
            mark::DiagnosticsRequest request;
            request.timing_enabled = true;
            request.level = mark::DiagnosticsLevel::Summary;
            request.scope = mark::ExecutionScope::Full;
            request.run_id = "112-recovery";
            mark::DetectorDiagnosticsAccess::prepare(detector, request);
            // 视频解码和 JSON 序列化在计时外；Summary、缓存创建与释放仍在 process 内。
            const auto start = std::chrono::steady_clock::now();
            const auto result = detector.process(input);
            const double elapsed =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count();
            const auto record = mark::DetectorDiagnosticsAccess::take(detector);
            std::cout << "{\"frame\":" << frame << ",\"status\":" << int(result.status)
                      << ",\"elapsed_ms\":" << elapsed << ",\"detections\":[";
            bool comma = false;
            for (const auto &detection : result.detections)
            {
                if (comma)
                    std::cout << ',';
                comma = true;
                std::cout << '[';
                for (size_t corner = 0; corner < 4; ++corner)
                {
                    if (corner)
                        std::cout << ',';
                    std::cout << '[' << detection.corners[corner].x << ','
                              << detection.corners[corner].y << ']';
                }
                std::cout << ']';
            }
            std::cout << "],\"timings\":{";
            comma = false;
            for (auto stage : {mark::Stage::Preprocess, mark::Stage::Detect, mark::Stage::Decode,
                               mark::Stage::Stabilize})
            {
                if (comma)
                    std::cout << ',';
                comma = true;
                const auto &sample = record.timings[static_cast<size_t>(stage)];
                std::cout << '"' << int(stage) << "\":"
                          << std::chrono::duration<double, std::milli>(
                                 sample.elapsed.value_or(std::chrono::nanoseconds{}))
                                 .count();
            }
            std::cout << "}}\n";
            ++frame;
        }
        std::cerr << "frames=" << frame << " threads=" << cv::getNumThreads() << " fps=" << fps
                  << '\n';
        return frame == 1676 ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
