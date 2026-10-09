// 内部阶段入口：接收完成M/S验证的批次，输出当前Detection及逐假设证据/原因。
// 不实现稳定层，不构造track，不改变公共Detector接口或FrameResult布局。
#pragma once
#include "corners/corner_types.hpp"
#include "mark/detector_types.hpp"
#include "core/prepared_frame.hpp"
#include "core/marker_geometry.hpp"
#include "mark/detector_config.hpp"
namespace mark {
class FrameDiagnosticsContext;
struct DecodeStageResult {
    Status status{Status::NOT_READY};
    std::vector<Detection> detections;
    std::vector<CornerMeasurement> measurements;
    std::vector<std::string> diagnostics;
    bool search_truncated{false};
};
DecodeStageResult decodeStage(const PreparedFrame&,const GeometryBatch&,const MarkerGeometry&,const CornerConfig&);
DecodeStageResult runDecodePipeline(const FrameInput&,const DetectorConfig&,const MarkerGeometry&);
DecodeStageResult decodeStage(const PreparedFrame&,const GeometryBatch&,const MarkerGeometry&,const CornerConfig&,FrameDiagnosticsContext*);
DecodeStageResult runDecodePipeline(const FrameInput&,const DetectorConfig&,const MarkerGeometry&,FrameDiagnosticsContext*);
}
