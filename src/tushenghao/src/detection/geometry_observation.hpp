#pragma once
#include "vision/types/prepared_frame_types.hpp"
#include "vision/config/detector_config.hpp"
namespace mark::detector {
std::vector<WhiteComponent> extract_white_components(const PreparedFrame&,const GeometryConfig&);
}
