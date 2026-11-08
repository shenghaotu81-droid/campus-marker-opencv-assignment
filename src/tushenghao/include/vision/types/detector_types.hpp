#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <opencv2/core.hpp>

namespace mark
{
    struct FrameInput
    {
        cv::Mat image;
        uint64_t frame_id{};
        int64_t timestamp_us{};
    };

    struct Detection
    {
        std::array<cv::Point2f, 4> corners;
        std::array<int, 4> orientation{{0, 1, 2, 3}};
    };
    enum class Status
    {
        INVALID_INPUT,
        NOT_DETECTED,
        DETECTED
    };

    struct FrameResult
    {
        uint64_t frame_id{};
        int64_t timestamp_us{};
        Status status{Status::INVALID_INPUT};
        std::vector<Detection> detections;
        std::vector<std::string> diagnostics;
    };
}
