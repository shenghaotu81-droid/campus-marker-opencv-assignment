// 原非法分支只返回错误而留历史；统一纯检查供实例生命周期使用。
#pragma once
#include "core/frame_stamp.hpp"
namespace mark {
bool validateFrameStamp(const FrameStamp&, const std::optional<FrameStamp>& previous, std::string& reason);
}
