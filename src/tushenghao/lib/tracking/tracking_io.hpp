#pragma once

#include "vision/types/tracking_types.hpp"
#include <filesystem>

namespace mark::tracking
{
// 原G2文件缺曝光/时间误差/协方差；兼容reader明确降级，不解析人类diagnostics字符串。
std::vector<TrackingFrame> read_tracking_frames(const std::filesystem::path &path);
void write_tracking_frames(const std::vector<TrackingFrame> &frames,
                           const std::filesystem::path &path);
void write_tracking_results(const std::vector<TrackingResult> &results,
                            const std::filesystem::path &path);
} // namespace mark::tracking
