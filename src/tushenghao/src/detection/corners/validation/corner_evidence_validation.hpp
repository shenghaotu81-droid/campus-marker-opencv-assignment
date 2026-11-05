// 输入单角当前观测证据，检查来源/完整性/数值合法性；残差和延伸仅为质量标签；只校验，不生成或修正角点。
#pragma once
#include "vision/types/detection/corner_types.hpp"
#include "vision/config/detector_config.hpp"
namespace mark {
bool validateCornerEvidence(const CornerEvidence&, cv::Point2d corner, int physical_index,
                            const CornerConfig&, std::string& reason);
}
