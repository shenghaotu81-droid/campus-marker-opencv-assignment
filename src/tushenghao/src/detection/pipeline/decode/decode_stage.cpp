// 当前帧：白片→match四边形→原图拟合→排序／校验→RAW发布；没有历史补框。
#include "pipeline/decode/decode_stage.hpp"
#include "preprocess/preprocess.hpp"
#include "geometry/observation/geometry_observation.hpp"
#include "corners/measurement/corner_resolver.hpp"
#include "corners/observation/corner_observation.hpp"
#include "corners/validation/detection_validator.hpp"
#include "corners/publication/detection_publication.hpp"
#include "detector/corner_budget_validation.hpp"
namespace mark {
DecodeStageResult decodeStage(const PreparedFrame &frame,const std::vector<MatchQuad> &matches,
    const CornerConfig &config) {
    DecodeStageResult result;
    if(!config.observation_budget_) {result.diagnostics.push_back("CORNER_BUDGET_NOT_CONFIGURED");return result;}
    validateCornerObservationBudget(config);
    result.status=Status::NOT_DETECTED;
    OriginalContourIndex index(frame);
    for(size_t i=0;i<matches.size();++i) {
        auto log=[&](const std::string &reason){result.diagnostics.push_back("match/"+std::to_string(i)+"/"+reason);};
        ScreenOrder order;
        auto corner=resolveObservedCorners(frame,matches[i],config,index,&order);
        if(!corner.measurement_){log(corner.rejection_reason_);continue;}
        auto valid=validateDetectionGeometry(*corner.measurement_,order,frame.original_image_.size(),config);
        if(!valid.valid_){log(valid.rejection_reason_);continue;}
        std::string reason;
        // match已固定物理身份，不再搜索/归并方向解释。
        auto detection=publishFloatDetection(*corner.measurement_,true,frame.original_image_.size(),config,reason);
        if(!detection){log(reason);continue;}
        result.measurements.push_back(std::move(*corner.measurement_));
        result.detections.push_back(std::move(*detection));
    }
    if(!result.detections.empty())result.status=Status::DETECTED;
    return result;
}
DecodeStageResult runDecodePipeline(const FrameInput &input,const DetectorConfig &config) {
    DecodeStageResult result;
    if(input.image.empty() || input.image.dims!=2 || input.image.type()!=CV_8UC3 ||
       input.timestamp_us<0 || input.time_source!=TimestampSource::Unknown) {
        result.status=Status::INVALID_INPUT;result.diagnostics.push_back("INPUT_FORMAT");return result;
    }
    auto frame=preprocess(input,config.preprocess);
    auto components=extractWhiteComponents(frame,config.geometry_);
    return decodeStage(frame,matchQuads(frame,components,config.geometry_),config.corner_);
}
}
