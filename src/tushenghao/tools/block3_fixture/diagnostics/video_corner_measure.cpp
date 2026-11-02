#include "model/marker_geometry_loader.hpp"
// 用户授权实拍预算测量：固定原713完整补全帧，逐角测量，不因首角失败丢掉其余角。
// 仅解除待求交点弧距上限；其余支持/关联/病态约束保持冻结。不读人工真值。
#include "detector/config.hpp"
#include "preprocess/preprocess.hpp"
#include "geometry/observation/geometry_observation.hpp"
#include "geometry/parent/geometry_matcher.hpp"
#include "geometry/validation/geometry_validation.hpp"
#include "geometry/completion/geometry_assignment_completion.hpp"
#include "corners/observation/corner_observation.hpp"
#include "corners/fitting/corner_edge_fit.hpp"
#include "vision/math/observed_geometry.hpp"
#include <nlohmann/json.hpp>
#include <opencv2/videoio.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <map>
using Json = nlohmann::json;

// 每帧保存同一冻结链路的六片身份/父变换，后续弧修改测量复用它们，不重选生产者。
Json parentRecord(const mark::GeometryHypothesis &p)
{
    Json r;
    for (const auto &a : p.assignments_)
        r["assignments"].push_back({{"part", a.model_part_id_}, {"component", a.component_id_}});
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 3; ++x)
            r["affine"].push_back(p.affine_transform_.at<double>(y, x));
    return r;
}

// 只恢复内部测量父；来源是前次真实生产者，不用模型真值重估矩阵。
mark::GeometryHypothesis restoreParent(const Json &r)
{
    mark::GeometryHypothesis p;
    p.affine_transform_ = cv::Mat(2, 3, CV_64F);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 3; ++x)
            p.affine_transform_.at<double>(y, x) = r.at("affine").at(y * 3 + x);
    for (const auto &a : r.at("assignments"))
        p.assignments_.push_back({a.at("part"), a.at("component")});
    return p;
}

int main(int argc, char **argv)
{
    try
    {
        if (argc != 5 && argc != 6)
            throw std::runtime_error(
                "video_corner_measure <video> <freeze> <old audit> <new jsonl> [fixed parent jsonl]");
        if (std::filesystem::exists(argv[4]))
            throw std::runtime_error("refusing overwrite");
        std::set<size_t> selected;
        std::ifstream audit(argv[3]);
        std::string line;
        while (std::getline(audit, line))
        {
            auto r = Json::parse(line);
            for (auto s : r.at("diagnostics"))
                if (s.get<std::string>() == "assignment/output_branches=1")
                    selected.insert(r.at("frame_id").get<size_t>());
        }
        if (selected.size() != 713)
            throw std::runtime_error("baseline713 membership mismatch");
        std::map<size_t, Json> fixed;
        if (argc == 6)
        {
            std::ifstream in(argv[5]);
            while (std::getline(in, line))
            {
                auto r = Json::parse(line);
                fixed.emplace(r.at("frame_id"), r.at("parent"));
            }
            if (fixed.size() != 713)
                throw std::runtime_error("fixed parent coverage mismatch");
        }
        auto cfg = mark::loadConfig(argv[2]).detector_config;
        auto model = mark::loadMarkerGeometry(cfg.marker_geometry_path_);
        auto measure = cfg.corner_;
        measure.max_corner_error_ = std::numeric_limits<double>::max();
        cv::VideoCapture cap(argv[1]);
        if (!cap.isOpened())
            throw std::runtime_error("video open failed");
        cv::Mat image;
        std::ofstream out(argv[4]);
        size_t count = 0, success = 0;
        for (size_t id = 0; cap.read(image); ++id)
            if (selected.count(id))
            {
                mark::FrameInput input{};
                input.image = image;
                input.frame_id = id;
                auto frame = mark::preprocess(input, cfg.preprocess);
                frame.components_ = mark::extractWhiteComponents(frame, cfg.geometry_);
                mark::GeometryHypothesis parent;
                if (argc == 6)
                    parent = restoreParent(fixed.at(id));
                else
                {
                    auto observations = mark::observeShapes(frame.components_, cfg.geometry_);
                    auto parents = mark::validateGeometryBatch(
                        mark::generateGeometryHypotheses(observations, model, cfg.geometry_), model,
                        frame.components_, cfg.geometry_);
                    auto complete = mark::completeSegmentedAssignments(
                        parents, frame.components_, model, *cfg.assignment_completion_);
                    if (complete.hypotheses_.size() != 1)
                        throw std::runtime_error("baseline branch changed at frame " +
                                                 std::to_string(id));
                    parent = complete.hypotheses_.front();
                }
                Json r = {{"frame_id", id},
                          {"parent", parentRecord(parent)},
                          {"diagnostic_only", true},
                          {"pending_corner_upper_applied", false},
                          {"fixed_baseline_parent", argc == 6}};

                struct Binding
                {
                    const char *part;
                    int a, b;
                };

                const Binding bindings[] = {{"L0", 5, 0}, {"M1", 0, 1}, {"L2", 5, 0}, {"L3", 5, 0}};
                for (int pi = 0; pi < 4; ++pi)
                {
                    const auto &bind = bindings[pi];
                    const mark::WhiteComponent *component = nullptr;
                    const mark::GeometryPolygon *polygon = nullptr;
                    for (auto a : parent.assignments_)
                        if (a.model_part_id_ == bind.part)
                            for (auto &c : frame.components_)
                                if (c.component_id_ == a.component_id_)
                                    component = &c;
                    for (auto &p : model.polygons)
                        if (p.id == bind.part)
                            polygon = &p;
                    if (!component || !polygon)
                        throw std::runtime_error("fixed identity missing");
                    auto contour = mark::observeOriginalContour(frame, *component,
                                                                *cfg.corner_.observation_budget_);
                    Json c = {{"physical", pi},
                              {"component", component->component_id_},
                              {"success", false},
                              {"error_px", nullptr},
                              {"reason", contour.reason}};
                    if (contour.reason.empty())
                    {
                        std::array<std::array<cv::Point2d, 2>, 2> edges;
                        for (int e = 0; e < 2; ++e)
                            for (int k = 0; k < 2; ++k)
                                edges[e][k] = frame.workToOriginal(mark::observed::project(
                                    parent.affine_transform_,
                                    polygon->vertices[((e ? bind.b : bind.a) + k) %
                                                      polygon->vertices.size()]));
                        auto fit = mark::fitObservedEdgePair(contour.contour, edges, measure);
                        c["reason"] = fit.reason;
                        if (fit.evidence)
                        {
                            auto &ev = *fit.evidence;
                            c["success"] = true;
                            c["error_px"] = ev.corner_error_px_;
                            c["intersection"] = {ev.intersection_.x, ev.intersection_.y};
                            c["line_residual_px"] = ev.line_mean_residual_px_;
                            c["extension_px"] = ev.support_extension_px_;
                            c["source_segment_start"] = ev.observed_segment_ids_;
                            c["source_segment_end"] = ev.observed_segment_end_ids_;
                            ++success;
                        }
                    }
                    r["corners"].push_back(c);
                }
                out << r.dump() << '\n';
                ++count;
                if (count % 100 == 0)
                    std::cout << "frames=" << count << " measurable_corners=" << success
                              << std::endl;
            }
        if (count != 713)
            throw std::runtime_error("video baseline frame coverage missing");
        std::cout << "frames=" << count << " corner_denominator=" << count * 4
                  << " measurable=" << success << '\n';
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
