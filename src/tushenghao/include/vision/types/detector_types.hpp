// 定义 Detector 系统中会传递的数据类型。(初步架构)（定义数据长什么样）（公共接口层组分）
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <opencv2/core.hpp>


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

    enum DetectionQualityFlag : uint32_t { LineFitResidualHigh = 1u, CornerErrorHigh = 2u, SupportExtensionHigh = 4u };

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

    // Detector 处理完一帧后的当前帧RAW输出（状态、检测结果与报错）
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
