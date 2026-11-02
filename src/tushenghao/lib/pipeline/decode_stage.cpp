// 唯一阶段编排路径：原三L生成/验证→窄范围M/S补全→原图四角→语义→当前Detection。
// 原集成用工作图size校验原图角、无条件填方向且bbox空；这里分别修正这些输出约束。
#include "pipeline/decode_stage.hpp"
#include "preprocess/preprocess.hpp"
#include "geometry/geometry_observation.hpp"
#include "geometry/geometry_matcher.hpp"
#include "geometry/geometry_validation.hpp"
#include "geometry/geometry_assignment_completion.hpp"
#include "corners/corner_resolver.hpp"
#include "corners/corner_observation.hpp"
#include "corners/semantic_resolver.hpp"
#include "corners/detection_validator.hpp"
#include "corners/detection_publication.hpp"
#include "pipeline/diagnostics_context.hpp"
#include "detector/corner_budget_validation.hpp"
#include <algorithm>
#include <set>
#include <sstream>
#include <iomanip>

namespace mark
{
    DecodeStageResult decodeStage(const PreparedFrame &frame, const GeometryBatch &batch,
                                  const MarkerGeometry &model, const CornerConfig &config)
    {
        return decodeStage(frame, batch, model, config, nullptr);
    }

    DecodeStageResult decodeStage(const PreparedFrame &frame, const GeometryBatch &batch,
                                  const MarkerGeometry &model, const CornerConfig &config,
                                  FrameDiagnosticsContext *context)
    {
        return decodeStage(frame, frame.components_, batch, model, config, context);
    }

    // 观察入口保留旧签名wrapper；计时与类型化原因在编排处提取，不向底层算法加入判据。
    DecodeStageResult decodeStage(const PreparedFrame &frame, const std::vector<WhiteComponent> &components,
                                  const GeometryBatch &batch,
                                  const MarkerGeometry &model, const CornerConfig &config,
                                  FrameDiagnosticsContext *context)
    {
        std::optional<ScopedStageTimer> timer;
        if (context)
            timer.emplace(context->timing, Stage::Decode);
        auto execute = [&]()
        {
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
                    if (context)
                        context->event(Stage::Decode,
                                       stage == "assignment"  ? ReasonCode::AssignmentInsufficient
                                       : stage == "screen"    ? ReasonCode::ScreenRejected
                                       : stage == "validator" ? ReasonCode::ValidationRejected
                                       : reason == "CLEARLY_INCOMPLETE"
                                           ? ReasonCode::ClearlyIncomplete
                                           : ReasonCode::CornerRejected,
                                       reason, h);
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
                if (context)
                    context->event(Stage::Decode, ReasonCode::UnresolvedCompetition,
                                   result.diagnostics.back());
                return result;
            }

            auto semantics = resolveSemantics(result.measurements, result.search_truncated, config, &orders);
            if (!semantics.geometry_consistent_)
            {
                result.diagnostics.push_back("semantic/" + semantics.rejection_reason_);
                if (context)
                    context->event(Stage::Decode, ReasonCode::SemanticRejected,
                                   semantics.rejection_reason_);
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
                    if (context)
                        context->event(Stage::Decode, ReasonCode::PublishFloatRejected, reason);
                    return result;
                }
                pending.push_back(*detection);
            }

            result.detections = std::move(pending);
            if (!result.detections.empty())
                result.status = Status::DETECTED;
            return result;
        };

        auto result = execute();
        if (context)
        {
            context->record.counts.measurements = result.measurements.size();
            context->record.counts.detections = result.detections.size();
            context->record.result_status = result.status;
            if (context->details)
                context->details->decoded = result;
        }
        return result;
    }

    GeometryStepsOutput runGeometrySteps(const FrameInput &input, const DetectorConfig &config,
                                         const MarkerGeometry &model, GeometryStop stop,
                                         FrameDiagnosticsContext *context, bool pipeline_trace)
    {
        GeometryStepsOutput output;
        auto &prepared = output.prepared;
        {
            std::optional<ScopedStageTimer> timer;
            if (context)
                timer.emplace(context->timing, Stage::Preprocess);
            prepared = preprocess(input, config.preprocess);
            if (context)
            {
                context->record.work_size = prepared.image_.size();
                if (context->details)
                    context->details->prepared = prepared;
            }
        }
        std::optional<ScopedStageTimer> detect_timer;
        if (context)
            detect_timer.emplace(context->timing, Stage::Detect);
        auto &components = output.components;
        components = extractWhiteComponents(prepared, config.geometry_);
        auto &observations = output.observations;
        observations = observeShapes(components, config.geometry_);
        output.generated = generateGeometryHypotheses(observations, model, config.geometry_);
        auto batch = output.generated;
        if (context)
        {
            context->record.counts.components = components.size();
            context->record.counts.observations = observations.size();
            context->record.counts.generated = batch.hypotheses_.size();
            if (context->details)
            {
                context->details->components = components;
                context->details->observations = observations;
                context->details->generated = batch;
            }
        }
        batch = validateGeometryBatch(batch, model, components, config.geometry_);
        output.validated = batch;
        if (context)
        {
            context->record.counts.validated = batch.hypotheses_.size();
            if (context->details)
                context->details->validated = batch;
        }
        if (stop == GeometryStop::Validated) return output;
        if (!config.assignment_completion_) throw std::logic_error("GEOMETRY_ASSIGNMENT_BUDGET_MISSING");
        if (pipeline_trace)
        {
        batch.diagnostics_.push_back("geometry/components=" + std::to_string(components.size()) +
                                     "/hypotheses=" + std::to_string(batch.hypotheses_.size()));
        // 保存实际三L父变换/绑定以定位上游反例；不重估、不改变其算法或参数。
        if (!context || context->record.selected)
            for (size_t i = 0; i < batch.hypotheses_.size(); ++i)
            {
                const auto &h = batch.hypotheses_[i];
                std::ostringstream trace;
                trace << std::setprecision(17) << "geometry/parent=" << i << "/affine=";
                if (h.affine_transform_.rows == 2 && h.affine_transform_.cols == 3 &&
                    h.affine_transform_.type() == CV_64F)
                    for (int row = 0; row < 2; ++row)
                        for (int col = 0; col < 3; ++col)
                            trace << h.affine_transform_.at<double>(row, col) << ',';
                trace << "/assignments=";
                for (const auto &a : h.assignments_)
                    trace << a.model_part_id_ << ':' << a.component_id_ << ',';
                trace << "/residual=" << h.validation_residual_
                      << "/completeness=" << int(h.completeness_);
                batch.diagnostics_.push_back(trace.str());
            }
        }
        batch =
            completeSegmentedAssignments(batch, components, model, *config.assignment_completion_);
        if (context)
        {
            context->record.counts.completed = batch.hypotheses_.size();
            context->record.counts.truncated = batch.resource_truncated_;
            for (const auto &reason : batch.diagnostics_)
                if (reason.rfind("geometry/parent=", 0) != 0)
                    context->event(Stage::Detect, ReasonCode::OtherRecordedReason, reason);
            if (context->details)
            {
                context->details->prepared = prepared;
                context->details->completed = batch;
            }
        }
        output.completed = std::move(batch);
        detect_timer.reset();
        return output;
    }

    DecodeStageResult runDecodePipeline(const FrameInput &input, const DetectorConfig &config,
                                        const MarkerGeometry &model)
    {
        return runDecodePipeline(input, config, model, ExecutionScope::Full, nullptr);
    }

    // 分阶段包围实际调用，详细trace只在选中帧构造；旧wrapper保留旧行为供历史工具使用。
    DecodeStageResult runDecodePipeline(const FrameInput &input, const DetectorConfig &config,
                                        const MarkerGeometry &model,
                                        ExecutionScope scope, FrameDiagnosticsContext *context)
    {
        DecodeStageResult result;
        if (input.image.empty() || input.image.dims != 2 || input.image.type() != CV_8UC3 ||
            input.timestamp_us < 0 || input.time_source != TimestampSource::Unknown)
        {
            result.status = Status::INVALID_INPUT;
            result.diagnostics.push_back("INPUT_FORMAT");
            if (context)
            {
                context->event(Stage::Preprocess, ReasonCode::InputFormat, "INPUT_FORMAT");
                if (scope == ExecutionScope::Geometry)
                    context->record.geometry_scope_result = "INVALID_INPUT";
                else
                    context->record.result_status = result.status;
            }
            return result;
        }
        // 原 Geometry 审计被后续 corner 预算拦住；此处只检查几何实际消费的 assignment 预算。
        if (!config.assignment_completion_)
        {
            result.diagnostics.push_back("PIPELINE_NOT_READY: assignment及原图定位预算未配置");
            if (context)
            {
                context->event(Stage::Preprocess, ReasonCode::BudgetMissing,
                               result.diagnostics.back());
                if (scope == ExecutionScope::Geometry)
                    context->record.geometry_scope_result = "NOT_READY";
                else
                    context->record.result_status = result.status;
            }
            return result;
        }
        auto geometry = runGeometrySteps(input, config, model, GeometryStop::Completed, context, true);
        if (scope == ExecutionScope::Geometry)
        {
            if (context)
            {
                context->record.result_status.reset();
                context->record.geometry_scope_result = "READY";
            }
            return result;
        }
        // Geometry 已返回；角点预算只在其消费者之前检查，公共 Detector 原生产门控保留。
        if (!config.corner_.observation_budget_)
        {
            result.diagnostics.push_back("PIPELINE_NOT_READY: assignment及原图定位预算未配置");
            if (context)
            {
                context->event(Stage::Decode, ReasonCode::BudgetMissing, result.diagnostics.back());
                context->record.result_status = result.status;
            }
            return result;
        }
        result = decodeStage(geometry.prepared, geometry.components, geometry.completed, model, config.corner_, context);
        return result;
    }
}
