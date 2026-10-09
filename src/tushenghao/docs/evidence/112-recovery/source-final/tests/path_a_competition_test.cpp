// Path A 集成与反证：前置资格不允许豁免合法父的角点取证失败。
#include "observability_fixture.hpp"
#include "geometry/geometry_anchor_evidence.hpp"
#include "geometry/geometry_l_topology.hpp"
#include "geometry/geometry_observation.hpp"
#include "geometry/geometry_matcher.hpp"
#include "geometry/geometry_validation.hpp"
#include "geometry/geometry_assignment_completion.hpp"
#include "pipeline/decode_stage.hpp"
#include "corners/corner_resolver.hpp"
#include "corners/detection_validator.hpp"
#include "corners/semantic_resolver.hpp"
#include "mark/detector.hpp"
using namespace mark;

namespace
{
    using temporal_fixture::check;

    struct Chain
    {
        fixture::Scene scene;
        DetectorConfig config;
        std::vector<ShapeObservation> observations;
        GeometryBatch batch;
        DecodeStageResult decoded;
    };

    Chain chain()
    {
        Chain c;
        c.scene = fixture::scene();
        c.config = observability_fixture::app().detector_config;
        c.scene.frame.components_ = extractWhiteComponents(c.scene.frame, c.config.geometry_);
        c.observations = observeShapes(c.scene.frame.components_, c.config.geometry_);
        c.batch = completeSegmentedAssignments(
            validateGeometryBatch(
                generateGeometryHypotheses(c.observations, c.scene.geometry, c.config.geometry_),
                c.scene.geometry, c.scene.frame.components_, c.config.geometry_),
            c.scene.frame.components_, c.scene.geometry, *c.config.assignment_completion_);
        c.decoded = decodeStage(c.scene.frame, c.batch, c.scene.geometry, c.config.corner_);
        check(c.decoded.status == Status::DETECTED && !c.decoded.measurements.empty(),
              "real chain lost detection");
        return c;
    }

    void sources(const Chain &c, const GeometryHypothesis &h)
    {
        size_t n = 0;
        for (auto &a : h.assignments_)
            if (a.model_part_id_[0] == 'L')
            {
                auto o = std::find_if(c.observations.begin(), c.observations.end(),
                                      [&](auto &o)
                                      {
                                          return o.source_component_id_ == a.component_id_;
                                      });
                check(o != c.observations.end(), "missing current source");
                auto collected = collect_observed_l_anchors(*o, a.component_id_);
                check(!collected.anchors_.empty(), "unsupported L");
                std::string prefix = "anchor_topology/v1 part=" + a.model_part_id_ +
                                     " component=" + std::to_string(a.component_id_) + " ";
                check(std::count_if(h.evidence_.begin(), h.evidence_.end(),
                                    [&](auto &e)
                                    {
                                        return e.rfind(prefix, 0) == 0;
                                    }) == 1,
                      "missing unique anchor evidence");
                ++n;
            }
        check(n == 3, "not three L");
    }

    void a10()
    {
        auto c = chain();
        for (auto &h : c.batch.hypotheses_)
            sources(c, h);
        for (auto &m : c.decoded.measurements)
        {
            auto order = orderScreenCorners(m.physical_corners_, c.config.corner_);
            check(order.status_ == ScreenOrderStatus::SUCCESS, "screen order invalid");
            auto v = validateDetectionGeometry(
                m, *order.screen_order_, c.scene.frame.original_image_.size(), c.config.corner_);
            check(v.valid_, v.rejection_reason_);
            for (auto &e : m.evidence_)
                check(e.original_observation_ && e.frame_id_ == c.scene.frame.frame_id_ &&
                          std::any_of(c.scene.frame.components_.begin(),
                                      c.scene.frame.components_.end(),
                                      [&](auto &w)
                                      {
                                          return w.component_id_ == e.component_id_;
                                      }),
                      "noncurrent evidence");
        }
    }

    void a11()
    {
        auto c = chain();
        auto good = c.batch.hypotheses_.at(0), failed = good;
        // 测试侧增加当前真实白色矩形作为另一 M 对应：引用／六片格式合法，三 L 原值与来源不变。
        // 它有原图连续轮廓，但不具预测 M 的必要邻边；不能以重复占用／非法 affine 冒充取证失败。
        c.scene.frame.original_image_ = c.scene.frame.original_image_.clone();
        cv::rectangle(c.scene.frame.original_image_, cv::Rect(200, 200, 50, 50),
                      cv::Scalar(255, 255, 255), cv::FILLED);
        FrameInput input{c.scene.frame.original_image_, c.scene.frame.frame_id_, 0,
                         TimestampSource::Unknown};
        auto pre = c.config.preprocess;
        pre.work_width = c.scene.frame.image_.cols;
        pre.work_height = c.scene.frame.image_.rows;
        auto extra = preprocess(input, pre);
        auto parts = extractWhiteComponents(extra, c.config.geometry_);
        auto it = std::find_if(parts.begin(), parts.end(),
                               [](auto &w)
                               {
                                   return w.bounding_box_.x == 200;
                               });
        check(it != parts.end(), "extra actual component missing");
        auto rectangle = *it;
        rectangle.component_id_ = 9091;
        c.scene.frame.components_.push_back(rectangle);
        for (auto &a : failed.assignments_)
            if (a.model_part_id_ == "M1")
                a.component_id_ = rectangle.component_id_;
        sources(c, failed);
        auto r = resolveObservedCorners(c.scene.frame, failed, c.scene.geometry, c.config.corner_);
        check(r.status_ == CornerResolutionStatus::FAILED &&
                  r.rejection_reason_.find("NO_VALID_ADJACENT_EDGES") != std::string::npos,
              "failure must reach original edge evidence: " + r.rejection_reason_);
        GeometryBatch b = c.batch;
        b.hypotheses_ = {good, failed};
        auto result = decodeStage(c.scene.frame, b, c.scene.geometry, c.config.corner_);
        check(result.status == Status::NOT_DETECTED && result.detections.empty() &&
                  !result.measurements.empty(),
              "failed legal competitor was exempted");
        check(std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                          [](auto &d)
                          {
                              return d.find("UNRESOLVED_COMPETING_GEOMETRY") != std::string::npos;
                          }),
              "lost conservative unresolved");
    }

    void a12()
    {
        auto c = chain();
        auto first = c.decoded.measurements.at(0);
        auto unique = resolveSemantics({first}, false, c.config.corner_);
        check(unique.geometry_consistent_ && unique.orientation_unique_, "baseline semantics");
        auto truncated = resolveSemantics({first}, true, c.config.corner_);
        check(truncated.geometry_consistent_ && !truncated.orientation_unique_ &&
                  truncated.retained_measurements_.size() == 1,
              "truncation claimed unique");
        auto other = first;
        for (size_t i = 0; i < 4; ++i)
        {
            other.physical_corners_[i] = first.physical_corners_[(i + 2) % 4];
            other.evidence_[i] = first.evidence_[(i + 2) % 4];
            other.evidence_[i].physical_corner_ = static_cast<PhysicalCorner>(i);
        }
        auto ambiguous = resolveSemantics({first, other}, false, c.config.corner_);
        check(ambiguous.geometry_consistent_ && !ambiguous.orientation_unique_ &&
                  ambiguous.retained_measurements_.size() == 1,
              "same geometry orientation conflict lost");
        for (size_t i = 0; i < 4; ++i)
        {
            other.physical_corners_[i] += cv::Point2d(100, 100);
            other.evidence_[i].intersection_ = other.physical_corners_[i];
        }
        auto conflict = resolveSemantics({first, other}, false, c.config.corner_);
        check(!conflict.geometry_consistent_ && conflict.retained_measurements_.empty(),
              "different geometry accepted");
        auto high = first;
        for (auto &e : high.evidence_)
            e.error_ += 1;
        auto lower = resolveSemantics({high, first}, false, c.config.corner_);
        check(lower.retained_measurements_.size() == 1 &&
                  lower.retained_measurements_[0].evidence_[0].error_ == first.evidence_[0].error_,
              "residual choice changed");
    }

    void rawEqual(const FrameResult &a, const FrameResult &b)
    {
        check(a.status == b.status && a.detections.size() == b.detections.size(),
              "raw status/state changed");
        for (size_t i = 0; i < a.detections.size(); ++i)
            check(temporal_fixture::same(a.detections[i], b.detections[i]),
                  "raw fields changed with history");
    }

    void a13()
    {
        auto cfg = observability_fixture::app().detector_config;
        auto image = fixture::scene().frame.original_image_;
        Detector on(cfg);
        auto offcfg = cfg;
        offcfg.temporal.stabilization_enabled = false;
        Detector off(offcfg);
        auto first = on.process({image, 0, 0, TimestampSource::Unknown});
        check(first.status == Status::DETECTED, "public real image failed");
        for (uint64_t id = 1; id < 4; ++id)
        {
            auto a = on.process({image, id, int64_t(id * 14000), TimestampSource::Unknown});
            auto b = off.process({image, id, int64_t(id * 14000), TimestampSource::Unknown});
            rawEqual(first, a);
            rawEqual(first, b);
        }
        auto empty = on.process(
            {cv::Mat::zeros(image.size(), image.type()), 4, 56000, TimestampSource::Unknown});
        check(empty.status == Status::NOT_DETECTED && empty.detections.empty() &&
                  empty.tracks.empty(),
              "empty borrowed history geometry");
        on.reset(ResetReason::InputChanged);
        rawEqual(first, on.process({image, 0, 0, TimestampSource::Unknown}));
    }
}

int main()
{
    return temporal_fixture::run({{"A10", a10}, {"A11", a11}, {"A12", a12}, {"A13", a13}});
}
