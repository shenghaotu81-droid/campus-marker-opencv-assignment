#pragma once
#include "vision/types/detection/prepared_image.hpp"
#include "vision/config/detector_config.hpp"
namespace mark {
std::vector<WhiteComponent> extractWhiteComponents(const PreparedFrame&,const GeometryConfig&);
}
