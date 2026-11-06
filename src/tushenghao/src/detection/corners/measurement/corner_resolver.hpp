#pragma once
#include "vision/types/detection/corner_types.hpp"
namespace mark
{
    class OriginalContourIndex;
    CornerResolution resolveObservedCorners(const CornerPoints &physical, OriginalContourIndex &);
}
