// 原非法分支只返回错误而留历史；统一纯检查供实例生命周期使用。
#pragma once
#include <optional>
#include <cstdint>
#include <string>
namespace mark
{
    struct FrameStamp
    {
        uint64_t frame_id{};
        int64_t timestamp_us{};
    };
    bool validateFrameStamp(const FrameStamp &, const std::optional<FrameStamp> &previous, std::string &reason);
}
