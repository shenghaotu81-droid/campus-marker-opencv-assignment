#pragma once
#include "vision/types/detection/decode_result.hpp"
#include "vision/types/detection/prepared_image.hpp"
#include "vision/types/marker_geometry.hpp"
#include "vision/config/detector_config.hpp"
namespace mark {
DecodeStageResult decodeStage(const PreparedFrame&, const GeometryBatch&, const MarkerGeometry&, const CornerConfig&);
DecodeStageResult decodeStage(const PreparedFrame&, const std::vector<WhiteComponent>&,
    const GeometryBatch&, const MarkerGeometry&, const CornerConfig&);
DecodeStageResult runDecodePipeline(const FrameInput&, const DetectorConfig&, const MarkerGeometry&);
}
