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
        // 继承自 FrameInput（frame_id_、timestamp_us_），打上 Status（status_），再加上 detections_（检到的角点列表）和 diagnostics_（诊断信息）
        uint64_t frame_id_{};
        int64_t timestamp_us_{};
        Status status_{Status::INVALID_INPUT};
        std::vector<Detection> detections_;
        std::vector<std::string> diagnostics_;
    };
}
