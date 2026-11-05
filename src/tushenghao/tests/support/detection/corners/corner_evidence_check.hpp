#pragma once
#include "vision/types/detection/corner_types.hpp"
#include "vision/config/detector_config.hpp"
namespace mark {
// 测试专用：从完整像素重算，检查拟合结果没有谎报残差/参数/延伸。
bool verifyCornerEvidence(const CornerEvidence&, cv::Point2d, int, const CornerConfig&, std::string&);
}
