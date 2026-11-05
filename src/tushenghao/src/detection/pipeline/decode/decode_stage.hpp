#pragma once
#include "vision/types/detection/decode_result.hpp"
#include "vision/types/detection/prepared_image.hpp"
#include "vision/config/detector_config.hpp"
#include "geometry/matching/match_quad.hpp"
namespace mark {
DecodeStageResult decodeStage(const PreparedFrame&,const std::vector<MatchQuad>&,const CornerConfig&);
DecodeStageResult runDecodePipeline(const FrameInput&,const DetectorConfig&);
}
