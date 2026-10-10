// 输入单角当前观测证据，复算有限支持/残差/连接弧误差；只校验，不生成或修正角点。
#pragma once
#include "corners/corner_types.hpp"
#include "mark/detector_config.hpp"
namespace mark {
bool validateCornerEvidence(const CornerEvidence&, cv::Point2d corner, int physical_index,
                            const CornerConfig&, std::string& reason);
}
