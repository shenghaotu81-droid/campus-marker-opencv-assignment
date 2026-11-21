#pragma once
#include "vision/types/prepared_frame_types.hpp"
#include "vision/config/detector_config.hpp"
#include <array>

namespace mark::detector
{
    // 原图粗顶点凸环，不分配物理身份；空集合表示本帧没有完整匹配。
    using MatchQuad = std::array<cv::Point2d, 4>;  // 一组粗（从工作图匹配出来的大概角点，还没在原图上精修过）的（候选mark）四角（每个点是 cv::Point2d（double 精度的 x、y），坐标是原图坐标；4 个点围成一个凸四边形，就是一个候选 MARK 的粗轮廓）
    std::vector<MatchQuad> match_quads(const PreparedFrame &, const std::vector<WhiteComponent> &,
                                       const GeometryConfig &);
}
