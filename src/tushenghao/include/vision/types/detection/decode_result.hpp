// 当前帧解码步骤输出值；无算法实例或编排函数，供下一步骤与只读观测共用。
#pragma once
#include "vision/types/detector_types.hpp"
#include "vision/types/detection/corner_types.hpp"
namespace mark {
struct DecodeStageResult {
    Status status{Status::NOT_READY};
    std::vector<Detection> detections;
    std::vector<CornerMeasurement> measurements;
    std::vector<std::string> diagnostics;
};
}
