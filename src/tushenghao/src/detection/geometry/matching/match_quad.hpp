#pragma once
#include "vision/types/detection/prepared_image.hpp"
#include "vision/config/detector_config.hpp"
#include <array>
namespace mark {
// 原图粗顶点凸环，不分配物理身份；空集合表示本帧没有完整匹配。
using MatchQuad = std::array<cv::Point2d,4>;
std::vector<MatchQuad> matchQuads(const PreparedFrame&, const std::vector<WhiteComponent>&, const GeometryConfig&);
}
