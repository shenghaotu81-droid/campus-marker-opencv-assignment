// A07/V03：输入完整 FrameRecord，输出原版本 JSON；不管理运行或文件生命周期。
#pragma once
#include "vision/observability/frame_record.hpp"
#include <string>
namespace mark { std::string serializeFrameRecord(const FrameRecord &); }
