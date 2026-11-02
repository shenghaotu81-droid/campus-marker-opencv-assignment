#pragma once
#include "vision/synth_ref/model.hpp"
namespace sg {
// S05：沿支撑线偏移，输出连续顶点；非法偏移保留失败样例。
bool offset_polygon(const Polygon&,const std::vector<double>&,Polygon&);
}
