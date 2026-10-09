// 稳定点不是原图拟合交点；只验证几何/偏离，不伪造CornerEvidence。
#pragma once
#include "pipeline/temporal_types.hpp"
#include "mark/detector_config.hpp"
namespace mark {
CornerCorrespondence resolveCornerCorrespondence(const Detection&, const Detection& previous_raw, const TemporalConfig&);
bool validateStableCorners(const std::array<cv::Point2d,4>&, const Detection& current,
                           cv::Size original_size, double max_deviation_px, std::string& reason);
cv::Rect2f boundingBoxFromCorners(const std::array<cv::Point2f,4>&);
}
