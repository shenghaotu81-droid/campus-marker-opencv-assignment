#include "vision/types/detection/decode_result.hpp"
// 内部阶段入口：接收完成M/S验证的批次，输出当前Detection及逐假设证据/原因。
// 不实现稳定层，不构造track，不改变公共Detector接口或FrameResult布局。
#pragma once
#include "vision/types/detection/corner_types.hpp"
#include "vision/observability/frame_record.hpp"
#include "vision/types/detector_types.hpp"
#include "vision/types/detection/prepared_image.hpp"
#include "vision/types/marker_geometry.hpp"
#include "vision/config/detector_config.hpp"
namespace mark {
class FrameDiagnosticsContext;
// D02→D09：每步只读上一步产物；诊断工具可显式停在父验证，不补做无关步骤。
enum class GeometryStop { Validated, Completed };
struct GeometryStepsOutput {
 PreparedFrame prepared;
 std::vector<WhiteComponent> components;
 std::vector<ShapeObservation> observations;
 GeometryBatch generated, validated, completed;
};
GeometryStepsOutput runGeometrySteps(const FrameInput&, const DetectorConfig&, const MarkerGeometry&,
                                    GeometryStop = GeometryStop::Completed, FrameDiagnosticsContext* = nullptr, bool pipeline_trace = false);
DecodeStageResult decodeStage(const PreparedFrame&,const GeometryBatch&,const MarkerGeometry&,const CornerConfig&);
DecodeStageResult runDecodePipeline(const FrameInput&,const DetectorConfig&,const MarkerGeometry&);
DecodeStageResult decodeStage(const PreparedFrame&,const std::vector<WhiteComponent>&,
                             const GeometryBatch&,const MarkerGeometry&,const CornerConfig&,FrameDiagnosticsContext* = nullptr);
DecodeStageResult decodeStage(const PreparedFrame&,const GeometryBatch&,const MarkerGeometry&,const CornerConfig&,FrameDiagnosticsContext*);
DecodeStageResult runDecodePipeline(const FrameInput&,const DetectorConfig&,const MarkerGeometry&,ExecutionScope,FrameDiagnosticsContext* = nullptr);
}
