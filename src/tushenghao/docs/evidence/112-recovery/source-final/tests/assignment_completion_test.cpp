// 窄范围补全独立测试：真实fixture、非连续ID、已有绑定、错误片及资源边界。
#include "block3_fixture.hpp"
#include "geometry/geometry_assignment_completion.hpp"
#include "pipeline/decode_stage.hpp"
#include "geometry/assignment_match_metrics.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    void check(bool ok, const char *text)
    {
        if (!ok)
            throw std::runtime_error(text);
    }

    // 仅fixture预算，不能回写生产配置，也不用于正式C/H/视频通过率。
    mark::AssignmentCompletionConfig budget()
    {
        return {3, 1, 10, 0.08, 1, 100, 100, 100};
    }

    mark::GeometryBatch batch(const fixture::Scene &s, int assignments = 3)
    {
        mark::GeometryBatch b;
        b.hypotheses_.push_back(s.hypothesis);
        b.hypotheses_[0].assignments_.resize(assignments);
        return b;
    }

    mark::GeometryBatch complete(const fixture::Scene &s, int n = 3,
                                 mark::AssignmentCompletionConfig c = budget())
    {
        return mark::completeSegmentedAssignments(batch(s, n), s.frame.components_, s.geometry, c);
    }

    void sixPieces()
    {
        auto s = fixture::scene();
        auto b = complete(s);
        check(b.hypotheses_.size() == 1, "six pieces not completed");
        check(b.hypotheses_[0].assignments_.size() == 6, "assignment count wrong");
        check(cv::norm(b.hypotheses_[0].affine_transform_, s.hypothesis.affine_transform_) == 0,
              "parent affine changed");
        for (size_t i = 0; i < 3; ++i)
            check(b.hypotheses_[0].assignments_[i].component_id_ ==
                      s.hypothesis.assignments_[i].component_id_,
                  "threeL changed");
        check(!b.resource_truncated_, "complete search marked truncated");
    }

    void existingBindings()
    {
        auto s = fixture::scene();
        auto b = complete(s, 6);
        check(b.hypotheses_.size() == 1, "complete parent rejected");
        check(b.hypotheses_[0].evidence_ == s.hypothesis.evidence_,
              "complete parent got duplicate evidence");
        check(complete(s, 4).hypotheses_.size() == 1, "partial existing M lost");
        s.hypothesis.assignments_[3].component_id_ = s.hypothesis.assignments_[4].component_id_;
        check(complete(s, 4).hypotheses_.empty(), "bad existing binding silently replaced");
    }

    void duplicates()
    {
        auto s = fixture::scene();
        s.hypothesis.assignments_[1].component_id_ = s.hypothesis.assignments_[0].component_id_;
        check(complete(s).hypotheses_.empty(), "one piece assigned twice");
    }

    void wrongShape()
    {
        auto s = fixture::scene();
        for (auto &c : s.frame.components_)
            if (c.component_id_ == s.hypothesis.assignments_[3].component_id_)
            {
                c.contour_ = {{1052, 670}, {1080, 670}, {1080, 698}, {1052, 698}};
                c.area_ = cv::contourArea(c.contour_);
            }
        check(complete(s).hypotheses_.empty(), "nearest wrong rectangle accepted as M");
    }

    // 固定H栅格反例：原五比例均为4点；更细RDP的7点需保留凹口并消除额外凸点。
    void topologyFixture(const char *filename)
    {
        std::ifstream source(std::filesystem::path(__FILE__).parent_path() / "data" / filename);
        std::string header;
        std::getline(source, header);
        cv::Mat matrix(2, 3, CV_64F);
        for (int row = 0; row < 2; ++row)
            for (int col = 0; col < 3; ++col)
                source >> matrix.at<double>(row, col);
        double reference;
        mark::WhiteComponent c;
        size_t n;
        source >> reference >> c.area_ >> n;
        for (size_t i = 0; i < n; ++i)
        {
            cv::Point p;
            source >> p.x >> p.y;
            c.contour_.push_back(p);
        }
        check(bool(source), "fixed M contour missing");
        auto scene = fixture::scene();
        const mark::GeometryPolygon *m = nullptr;
        for (auto &part : scene.geometry.polygons)
            if (part.id == "M1")
                m = &part;
        auto metric = mark::measureAssignmentMatch(c, *m, matrix, reference, 416, 3, 1);
        check(metric.valid && metric.topology_valid, "RDP skipped six-vertex M topology");
        check(metric.boundary_distance <= 9.5 && metric.relative_area_error <= .045 &&
                  metric.direction_diff <= 30,
              "fixed approved M metrics failed");
    }

    void skippedTopology()
    {
        topologyFixture("ms-topology-skipped-six.txt");
    }

    void videoTopology()
    {
        topologyFixture("ms-video638-topology.txt");
    }

    void resourceBoundary()
    {
        auto s = fixture::scene();
        auto c = budget();
        c.max_expansions = 3;
        auto b = complete(s, 3, c);
        check(b.hypotheses_.size() == 1 && !b.resource_truncated_,
              "K exact exhaustion misreported");
        // 两个观测轮廓都合法且ID不同，必须保留竞争解释而不是取首个。
        auto duplicate = s.frame.components_[2]; // 用明确M引用查找，容器顺序不承载身份。
        for (auto piece : s.frame.components_)
            if (piece.component_id_ == s.hypothesis.assignments_[3].component_id_)
                duplicate = piece;
        duplicate.component_id_ = 999;
        s.frame.components_.push_back(duplicate);
        b = complete(s, 3, c);
        check(b.hypotheses_.size() == 1 && b.resource_truncated_,
              "remaining branch truncation lost");
        c.max_expansions = 6;
        b = complete(s, 3, c);
        check(b.hypotheses_.size() == 2 && !b.resource_truncated_,
              "valid competitors not retained");
        auto input = batch(s);
        input.resource_truncated_ = true;
        b = mark::completeSegmentedAssignments(input, s.frame.components_, s.geometry, c);
        check(b.resource_truncated_, "parent truncation lost");
        // 候选上限恰好扫描完与仍有未扫描组件分别核查，不能靠输出数量猜截断。
        s = fixture::scene();
        c = budget();
        c.max_candidates = 3;
        b = complete(s, 3, c);
        check(b.hypotheses_.size() == 1 && !b.resource_truncated_,
              "candidate K exact exhaustion misreported");
        auto extra = s.frame.components_.front();
        extra.component_id_ = 1000;
        s.frame.components_.push_back(extra);
        b = complete(s, 3, c);
        check(b.resource_truncated_, "candidate unsearched tail not marked truncated");
        s = fixture::scene();
        c = budget();
        c.max_output_branches = 1;
        b = complete(s, 3, c);
        check(b.hypotheses_.size() == 1 && !b.resource_truncated_,
              "output K exact exhaustion misreported");
        for (auto piece : s.frame.components_)
            if (piece.component_id_ == s.hypothesis.assignments_[3].component_id_)
                duplicate = piece;
        duplicate.component_id_ = 999;
        s.frame.components_.push_back(duplicate);
        b = complete(s, 3, c);
        check(b.hypotheses_.size() == 1 && b.resource_truncated_,
              "output competing tail not marked truncated");
    }

    void stageOutput()
    {
        auto s = fixture::scene();
        auto b = complete(s);
        auto c = fixture::cornerConfig();
        auto result = mark::decodeStage(s.frame, b, s.geometry, c);
        if (result.detections.empty())
            for (auto reason : result.diagnostics)
                std::cerr << reason << '\n';
        check(result.status == mark::Status::DETECTED && result.detections.size() == 1,
              "stage detection missing");
        const auto &d = result.detections[0];
        check(d.bbox == cv::Rect2f(920, 670, 160, 160), "bbox not from original min/max");
        check(bool(d.attributes.orientation) && !d.attributes.marker_code && !d.confidence,
              "stage attributes wrong");
        b.resource_truncated_ = true;
        result = mark::decodeStage(s.frame, b, s.geometry, c);
        check(result.detections.size() == 1 && !result.detections[0].attributes.orientation,
              "truncation became unique orientation");
        c.observation_budget_.reset();
        check(mark::decodeStage(s.frame, b, s.geometry, c).status == mark::Status::NOT_READY,
              "budget absence hidden");
        // D09原反例：角点远超480x360工作图，但在1440x1080原图内合法。
        auto small = fixture::scene(480, 360);
        // 此处隔离D09，不放宽assignment fixture门限：其0.08面积预算不足以覆盖小图
        // 栅格化，必须仍保守拒绝。六片身份由测试真值显式给定后单测阶段坐标契约。
        check(complete(small).hypotheses_.empty(),
              "unapproved small-scale assignment budget silently relaxed");
        auto small_batch = batch(small, 6);
        small_batch.segmented_assignments_ready_ = true;
        auto small_result =
            mark::decodeStage(small.frame, small_batch, small.geometry, fixture::cornerConfig());
        if (small_result.detections.empty())
            for (auto reason : small_result.diagnostics)
                std::cerr << reason << '\n';
        check(small_result.detections.size() == 1, "original corners validated against work size");
        small_batch.hypotheses_[0].assignments_.pop_back();
        check(mark::decodeStage(small.frame, small_batch, small.geometry, fixture::cornerConfig())
                  .detections.empty(),
              "missing S bypassed completion contract");
    }
}

int main()
{
    const std::pair<const char *, void (*)()> cases[] = {{"six_pieces", sixPieces},
                                                         {"existing_bindings", existingBindings},
                                                         {"duplicates", duplicates},
                                                         {"wrong_shape", wrongShape},
                                                         {"skipped_topology", skippedTopology},
                                                         {"video_topology", videoTopology},
                                                         {"resource_boundary", resourceBoundary},
                                                         {"stage_output", stageOutput}};
    int errors = 0;
    for (auto item : cases)
    {
        try
        {
            item.second();
            std::cout << "PASS " << item.first << '\n';
        }
        catch (const std::exception &e)
        {
            ++errors;
            std::cerr << "FAIL " << item.first << ": " << e.what() << '\n';
        }
    }
    return errors ? 1 : 0;
}
