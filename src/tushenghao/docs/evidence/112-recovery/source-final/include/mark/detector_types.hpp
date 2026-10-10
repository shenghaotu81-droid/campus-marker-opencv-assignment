// 定义 Detector 系统中会传递的数据类型。(初步架构)（定义数据长什么样）（公共接口层组分）
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

/*          快速恢复：NOTE
FrameInput
    |
    | 输入
    ↓

Detector

    |
    | 输出
    ↓

FrameResult
    |
    ├── detections
    |       当前检测
    |
    ├── tracks
    |       稳定结果
    |
    └── display_state
            显示历史状态
*/

namespace mark
{

    // MARK类别 -> C++ enum 映射
    enum class MarkCategory
    {
        Unknown = 0
    };

    // 时间来源 -> C++ enum 映射
    enum class TimestampSource
    {
        Unknown = 0
    };

    // MarkerCode：scheme（字符串）+ 非负 value 的编码结构
    struct MarkerCode
    {
        std::string scheme;

        int value;
    };

    // Attributes：附加语义信息
    struct Attributes
    {
        // 物理角部 -> 输出四点索引对应关系
        std::optional<std::array<int, 4>> orientation;

        // 可选编码信息
        std::optional<MarkerCode> marker_code;
    };

    // 输入帧（输入Detector）
    struct FrameInput
    {
        cv::Mat image;

        uint64_t frame_id;

        int64_t timestamp_us;

        TimestampSource time_source;
    };

    // 检测结果（输出important！）
    struct Detection
    {
        // MARK类别
        MarkCategory category;

        // 四个角点
        std::array<cv::Point2f, 4> corners;

        // 外接框
        cv::Rect2f bbox;

        // 附加属性
        Attributes attributes;

        // 可选置信度
        std::optional<float> confidence;

        // 质量标志位
        uint32_t quality_flags;
    };

    // 当前帧处理状态
    enum class Status
    {
        NOT_READY,
        INVALID_INPUT,
        NOT_DETECTED,
        DETECTED
    };

    // 一个稳定结果对应哪个当前检测。(时序稳定)
    struct TrackResult
    {
        // 当前稳定结果引用的当前检测索引
        // detection_index: index of current detection
        size_t detection_index;

        // 稳定结果
        // stable result
        Detection result;
    };

    // 显示层状态(用于显示和交互!=Detection看到（输出）的结果)
    struct DisplayState
    {
        // 来源帧
        // source frame
        uint64_t source_frame_id;

        // 历史年龄
        // age of held state
        uint64_t age;

        // 是否沿用历史显示
        // whether historical state is reused
        bool is_held;

        // G-ABI已批准选A：追加独立文字载荷，改变FrameResult嵌套ABI；所有目标须重建。
        std::string value;
    };

    // Detector 处理完一帧后的完整输出（包含状态、检测结果、稳定结果、显示状态等）
    struct FrameResult
    {
        // 帧号（输入）
        uint64_t frame_id;

        // 时间戳（输入）
        int64_t timestamp_us;

        // 当前处理状态
        Status status;

        // 当前检测结果
        std::vector<Detection> detections;

        // 稳定结果（时序稳定层）
        std::vector<TrackResult> tracks;

        // 可选历史显示状态
        std::optional<DisplayState> display_state;

        // Block5 已实现诊断字符串；类型与字段布局保持原契约。
        std::vector<std::string> diagnostics;
    };

    // 重置原因（仍属映射候选）（后续行为可能不同：void reset(ResetReason reason) noexcept;）
    enum class ResetReason
    {
        // 外部请求重置
        External,

        // 输入源变化
        InputChanged,

        // 输入序列非法
        InvalidSequence
    };

} // namespace mark
