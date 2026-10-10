#pragma once
#include "corners/corner_types.hpp"
#include "mark/detector_types.hpp"
#include <optional>
namespace mark {
// 原double排序在float舍入后可能不再规范；此入口仅发布已验证的物理测量，不重取证。
std::optional<Detection> publishFloatDetection(const CornerMeasurement& measurement,
    bool orientation_unique, cv::Size original_size, const CornerConfig& config,
    std::string& rejection_reason);
}
