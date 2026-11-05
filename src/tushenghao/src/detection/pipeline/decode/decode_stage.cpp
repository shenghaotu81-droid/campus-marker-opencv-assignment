// 当前帧编排：预处理→白片→父假设/对应→原图四角→语义→RAW发布。
#include "pipeline/decode/decode_stage.hpp"
#include "preprocess/preprocess.hpp"
#include "geometry/observation/geometry_observation.hpp"
#include "geometry/parent/geometry_matcher.hpp"
#include "geometry/validation/geometry_validation.hpp"
#include "geometry/completion/geometry_assignment_completion.hpp"
#include "corners/measurement/corner_resolver.hpp"
#include "corners/observation/corner_observation.hpp"
#include "corners/semantics/semantic_resolver.hpp"
#include "corners/validation/detection_validator.hpp"
#include "corners/publication/detection_publication.hpp"
#include "detector/corner_budget_validation.hpp"
#include <set>
namespace mark {
DecodeStageResult decodeStage(const PreparedFrame &frame, const GeometryBatch &batch,
    const MarkerGeometry &model, const CornerConfig &config) {
    return decodeStage(frame, frame.components_, batch, model, config);
}
DecodeStageResult decodeStage(const PreparedFrame &frame, const std::vector<WhiteComponent> &components,
    const GeometryBatch &batch, const MarkerGeometry &model, const CornerConfig &config) {
    DecodeStageResult result;
    result.diagnostics = batch.diagnostics_;
    result.search_truncated = batch.resource_truncated_;
    if (!batch.segmented_assignments_ready_ || !config.observation_budget_)
    {
        result.diagnostics.push_back("DECODE_NOT_READY: M/S验证或G3/G4预算尚未就绪");
        return result;
    }
    validateCornerObservationBudget(config);
    // 原来每个父假设的四角都重新分割原图；局部索引惰性准备且仅活过一次 decode。
    OriginalContourIndex original_index(frame, frame.frame_id_,
                                        *config.observation_budget_);
    result.status = Status::NOT_DETECTED;
    bool unresolved = false;
    std::vector<ScreenOrder> orders;
    for (size_t h = 0; h < batch.hypotheses_.size(); ++h)
    {
        const auto &hypothesis = batch.hypotheses_[h];
        auto log = [&](const std::string &stage, const std::string &reason)
        {
            result.diagnostics.push_back(stage + "/" + std::to_string(h) + "/" + reason);
        };
        if (hypothesis.completeness_ == GeometryCompleteness::CLEARLY_INCOMPLETE)
        {
            log("corner", "CLEARLY_INCOMPLETE");
            continue;
        }
        std::set<std::string> parts;
        for (const auto &a : hypothesis.assignments_)
            parts.insert(a.model_part_id_);
        if (parts != std::set<std::string>{"L0", "L2", "L3", "M1", "S1a", "S1b"})
        {
            log("assignment", "SEGMENTED_SUPPORT_INSUFFICIENT");
            unresolved = true;
            continue;
        }

        ScreenOrder order;
        auto corner =
            resolveObservedCorners(frame, components, hypothesis, model, config, original_index.source(), &order);
        if (!corner.measurement_)
        {
            log("corner", corner.rejection_reason_);
            unresolved = true;
            continue;
        }
        bool current_frame = true;
        for (const auto &e : corner.measurement_->evidence_)
            current_frame = current_frame && e.frame_id_ == frame.frame_id_;
        if (!current_frame)
        {
            log("validator", "EVIDENCE_FRAME_MISMATCH");
            unresolved = true;
            continue;
        }

        auto valid = validateDetectionGeometry(*corner.measurement_, order,
                                               frame.original_image_.size(), config);
        if (!valid.valid_)
        {
            log("validator", valid.rejection_reason_);
            unresolved = true;
            continue;
        }

        result.measurements.push_back(*corner.measurement_);
        orders.push_back(std::move(order));
    }
    // 不能因一个竞争假设取证失败，就将剩下一个强行声明为唯一方向/几何。
    if (unresolved && !result.measurements.empty())
    {
        result.diagnostics.push_back(
            "UNRESOLVED_COMPETING_GEOMETRY: 存在无法排除的失败候选");
        return result;
    }

    auto semantics = resolveSemantics(result.measurements, result.search_truncated, config, &orders);
    if (!semantics.geometry_consistent_)
    {
        result.diagnostics.push_back("semantic/" + semantics.rejection_reason_);
        return result;
    }
    // 原发布按double规范序直接转float，可能被稳定层拒绝；先完成全部发布再提交，失败竞争不能被删掉。
    std::vector<Detection> pending;
    for (const auto &measurement : semantics.retained_measurements_)
    {
        std::string reason;
        auto detection =
            publishFloatDetection(measurement, semantics.orientation_unique_,
                                  frame.original_image_.size(), config, reason);
        if (!detection)
        {
            result.diagnostics.push_back("publish_float/" + reason);
            return result;
        }
        pending.push_back(*detection);
    }

    result.detections = std::move(pending);
    if (!result.detections.empty())
        result.status = Status::DETECTED;
    return result;
}
DecodeStageResult runDecodePipeline(const FrameInput &input, const DetectorConfig &config,
    const MarkerGeometry &model) {
    DecodeStageResult result;
    if (input.image.empty() || input.image.dims != 2 || input.image.type() != CV_8UC3 ||
        input.timestamp_us < 0 || input.time_source != TimestampSource::Unknown) {
        result.status = Status::INVALID_INPUT;
        result.diagnostics.push_back("INPUT_FORMAT");
        return result;
    }
    if (!config.assignment_completion_ || !config.corner_.observation_budget_) {
        result.diagnostics.push_back("PIPELINE_BUDGET_MISSING");
        return result;
    }
    auto prepared = preprocess(input, config.preprocess);
    const auto components = extractWhiteComponents(prepared, config.geometry_);
    const auto observations = observeShapes(components, config.geometry_);
    auto batch = generateGeometryHypotheses(observations, model, config.geometry_);
    batch = validateGeometryBatch(batch, model, components, config.geometry_);
    batch = completeSegmentedAssignments(batch, components, model, *config.assignment_completion_);
    return decodeStage(prepared, components, batch, model, config.corner_);
}
} // namespace mark
