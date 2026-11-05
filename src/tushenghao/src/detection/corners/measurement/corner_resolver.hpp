#pragma once
#include "vision/types/detection/prepared_image.hpp"
#include "vision/types/detection/corner_types.hpp"
#include "vision/config/detector_config.hpp"
#include "geometry/matching/match_quad.hpp"
namespace mark {
class OriginalContourIndex;
CornerResolution resolveObservedCorners(const PreparedFrame&,const MatchQuad&,
    const CornerConfig&,OriginalContourIndex&,ScreenOrder* = nullptr);
}
