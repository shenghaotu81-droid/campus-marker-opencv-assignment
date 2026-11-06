#pragma once
#include "vision/types/detector_types.hpp"
namespace mark
{
    struct DecodeStageResult
    {
        Status status{Status::NOT_DETECTED};
        std::vector<Detection> detections;
        std::vector<std::string> diagnostics;
    };
}
