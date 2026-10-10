// 帧元数据独立于图像，避免三类历史缓存像素或共用隐式全局序列。
#pragma once
#include "mark/detector_types.hpp"
namespace mark {
struct FrameStamp {
    uint64_t frame_id{};
    int64_t timestamp_us{};
    TimestampSource time_source{TimestampSource::Unknown};
};
}
