#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <opencv2/core.hpp>

namespace mark::detector
{
    // FrameInput 结构体：表示一帧输入，包含图像、帧号和时间戳
    struct FrameInput
    {
        cv::Mat image_;
        uint64_t frame_id_{};
        int64_t timestamp_us_{};
    };

    struct Detection
    {
        std::array<cv::Point2f, 4> corners_;
        std::array<int, 4> orientation_{{0, 1, 2, 3}};
    };
    enum class Status
    {
        INVALID_INPUT,
        NOT_DETECTED,
        DETECTED
    };

    struct FrameResult
    {
        // 检测结果独立携带帧号和时间戳。
        uint64_t frame_id_{};
        int64_t timestamp_us_{};
        Status status_{Status::INVALID_INPUT};
        std::vector<Detection> detections_;
        std::vector<std::string> diagnostics_;
    };
}
