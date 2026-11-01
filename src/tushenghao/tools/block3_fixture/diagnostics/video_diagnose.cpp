// 只读视频归因：复用正式前半链路与原图拟合，诊断门限隔离不用于调参/验收。
#include "config/config.hpp"
#include "preprocess/preprocess.hpp"
#include "geometry/geometry_observation.hpp"
#include "geometry/geometry_matcher.hpp"
#include "geometry/geometry_validation.hpp"
#include "geometry/geometry_assignment_completion.hpp"
#include "corners/corner_observation.hpp"
#include "corners/corner_edge_fit.hpp"
#include "corners/corner_resolver.hpp"
#include "pipeline/decode_stage.hpp"
#include "core/observed_geometry_utils.hpp"
#include <nlohmann/json.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <set>
using Json = nlohmann::json;

// 原始候选弧指标仅用于解释拒绝：保存所有1/2相邻简化段，不替代正式fit入口。
Json arcMetrics(const std::vector<cv::Point> &contour, const mark::CornerConfig &c)
{
    std::vector<cv::Point> poly;
    cv::approxPolyDP(contour, poly, c.approximation_epsilon_, true);
    std::vector<size_t> indices;
    for (auto p : poly)
        indices.push_back(std::find(contour.begin(), contour.end(), p) - contour.begin());
    std::sort(indices.begin(), indices.end());
    Json rows = Json::array();
    for (size_t i = 0; i < indices.size(); ++i)
        for (size_t n = 1; n <= 2 && n < indices.size(); ++n)
        {
            size_t start = indices[i], end = indices[(i + n) % indices.size()];
            std::vector<cv::Point2d> pts;
            bool begun = false, ended = false, continuous = true;
            for (size_t k = start;; k = (k + 1) % contour.size())
            {
                cv::Point2d p = contour[k];
                if (cv::norm(p - cv::Point2d(contour[start])) >=
                        c.observation_budget_->turn_trim_distance_px &&
                    cv::norm(p - cv::Point2d(contour[end])) >=
                        c.observation_budget_->turn_trim_distance_px)
                {
                    if (ended)
                        continuous = false;
                    begun = true;
                    pts.push_back(p);
                }
                else if (begun)
                    ended = true;
                if (k == end)
                    break;
            }
            Json r = {
                {"begin", start}, {"end", end}, {"n", pts.size()}, {"continuous", continuous}};
            if (pts.size() >= 3)
            {
                cv::Vec4d line;
                cv::fitLine(pts, line, cv::DIST_L2, 0, 0.01, 0.01);
                double lo = INFINITY, hi = -INFINITY, sum = 0;
                for (auto p : pts)
                {
                    double t = p.x * line[0] + p.y * line[1];
                    lo = std::min(lo, t);
                    hi = std::max(hi, t);
                    sum += std::abs(line[0] * (p.y - line[3]) - line[1] * (p.x - line[2]));
                }
                r["span_px"] = hi - lo;
                r["mean_residual_px"] = sum / pts.size();
                r["fitted_line"] = {line[0], line[1], line[2], line[3]};
                r["support"] = Json::array();
                for (auto p : pts)
                    r["support"].push_back({p.x, p.y});
            }
            rows.push_back(r);
        }
    return rows;
}

enum class GateProbe { Frozen, Residual, Span, Points, Direction, Position, Corner, Extension, Connection, AllNumeric };
// V02：仅诊断副本关闭指定门限，数值、顺序和标签保持既有实验定义。
static mark::CornerConfig makeProbeConfig(const mark::CornerConfig &base, GateProbe probe)
{
    const int g = static_cast<int>(probe);
    auto c = base;
    auto &b = *c.observation_budget_;
    double high = std::numeric_limits<double>::max();
    if (g == 1 || g == 9)
        c.max_line_fit_error_ = high;
    if (g == 2 || g == 9)
        b.min_support_span_px = .001;
    if (g == 3 || g == 9)
        c.min_line_points_ = 3;
    if (g == 4 || g == 9)
        b.max_edge_direction_diff_deg = 90;
    if (g == 5 || g == 9)
        b.max_edge_position_distance_px = high;
    if (g == 6 || g == 9)
        c.max_corner_error_ = high;
    if (g == 7 || g == 9)
        b.max_support_extension_px = high;
    if (g == 8 || g == 9)
        b.max_turn_connection_length_px = high;
    return c;
}

int main(int argc, char **argv)
{
    try
    {
        if (argc != 4)
            throw std::runtime_error("video_diagnose <video> <frozen config> <new output dir>");
        std::filesystem::path dir = argv[3];
        if (std::filesystem::exists(dir))
            throw std::runtime_error("refusing overwrite");
        std::filesystem::create_directories(dir);
        auto cfg = mark::loadConfig(argv[2]).detector_config;
        auto model = mark::loadMarkerGeometry(cfg.marker_geometry_path_);
        if (!cfg.assignment_completion_ || !cfg.corner_.observation_budget_)
            throw std::runtime_error("explicit budgets required");
        cv::VideoCapture cap(argv[1]);
        if (!cap.isOpened())
            throw std::runtime_error("video open failed");
        cv::Mat image;
        std::set<int> selected{0, 1, 3, 4, 10, 36, 1200};
        std::ofstream out(dir / "diagnostics.jsonl");
        for (int id = 0; cap.read(image); ++id)
            if (selected.count(id))
            {
                mark::FrameInput input{};
                input.image = image;
                input.frame_id = id;
                auto steps = mark::runGeometrySteps(input, cfg, model, mark::GeometryStop::Completed);
                auto &frame = steps.prepared;
                const auto &generated = steps.generated;
                const auto &parents = steps.validated;
                // 旧单步探针入口继续使用兼容载体；主流程不借此回填共享状态。
                frame.components_ = steps.components;
                auto &batch = steps.completed;
                Json r = {{"frame_id", id},
                          {"diagnostic_only", true},
                          {"original_size", {image.cols, image.rows}},
                          {"work_size", {frame.image_.cols, frame.image_.rows}},
                          {"components", frame.components_.size()},
                          {"generated", generated.hypotheses_.size()},
                          {"validated", parents.hypotheses_.size()},
                          {"completed", batch.hypotheses_.size()},
                          {"parent_diagnostics", parents.diagnostics_},
                          {"assignment_diagnostics", batch.diagnostics_}};
                cv::imwrite((dir / ("frame-" + std::to_string(id) + ".png")).string(), image);
                for (size_t h = 0; h < batch.hypotheses_.size(); ++h)
                {
                    auto &parent = batch.hypotheses_[h];
                    // 独立关闭每项门限只作归因；生产配置、角点与全视频结果保持不变。
                    const char *gates[] = {"frozen",         "residual_off",  "span_off",
                                           "points_off",     "direction_off", "position_off",
                                           "corner_off",     "extension_off", "connection_off",
                                           "all_numeric_off"};
                    for (int g = 0; g < 10; ++g)
                    {
                        auto c = makeProbeConfig(cfg.corner_, static_cast<GateProbe>(g));
                        auto found = mark::resolveObservedCorners(frame, parent, model, c);
                        auto stage = mark::decodeStage(frame, batch, model, c);
                        Json gate = {{"branch", h},
                                     {"gate", gates[g]},
                                     {"four_corners", bool(found.measurement_)},
                                     {"reason", found.rejection_reason_},
                                     {"stage_detections", stage.detections.size()},
                                     {"stage_diagnostics", stage.diagnostics}};
                        for (auto &d : stage.detections)
                        {
                            Json points = Json::array();
                            for (auto p : d.corners)
                                points.push_back({p.x, p.y});
                            gate["detections"].push_back(
                                {{"corners", points},
                                 {"orientation_known", bool(d.attributes.orientation)}});
                        }
                        r["resolver_gates"].push_back(gate);
                    }

                    struct Binding
                    {
                        const char *part;
                        int a, b;
                    };

                    const Binding bindings[] = {
                        {"L0", 5, 0}, {"M1", 0, 1}, {"L2", 5, 0}, {"L3", 5, 0}};
                    for (int pi = 0; pi < 4; ++pi)
                    {
                        const auto &binding = bindings[pi];
                        size_t ci = 0;
                        for (auto a : parent.assignments_)
                            if (a.model_part_id_ == binding.part)
                                ci = a.component_id_;
                        const mark::WhiteComponent *component = nullptr;
                        for (auto &c : frame.components_)
                            if (c.component_id_ == ci)
                                component = &c;
                        auto observed = mark::observeOriginalContour(
                            frame, *component, *cfg.corner_.observation_budget_);
                        Json p = {{"physical", pi},
                                  {"part", binding.part},
                                  {"component", ci},
                                  {"observation_reason", observed.reason},
                                  {"contour_n", observed.contour.size()}};
                        if (observed.reason.empty())
                        {
                            const mark::GeometryPolygon *polygon = nullptr;
                            for (auto &part : model.polygons)
                                if (part.id == binding.part)
                                    polygon = &part;
                            std::array<std::array<cv::Point2d, 2>, 2> edges;
                            for (int e = 0; e < 2; ++e)
                                for (int j = 0; j < 2; ++j)
                                    edges[e][j] = frame.workToOriginal(mark::observed::project(
                                        parent.affine_transform_,
                                        polygon->vertices[((e == 0 ? binding.a : binding.b) + j) %
                                                          polygon->vertices.size()]));
                            auto fit =
                                mark::fitObservedEdgePair(observed.contour, edges, cfg.corner_);
                            p["fit_success"] = bool(fit.evidence);
                            p["fit_reason"] = fit.reason;
                            p["model_edges_original"] = Json::array();
                            for (auto edge : edges)
                                p["model_edges_original"].push_back(
                                    {{edge[0].x, edge[0].y}, {edge[1].x, edge[1].y}});
                            p["arcs"] = arcMetrics(observed.contour, cfg.corner_);
                            // 直接记录单角每门限隔离，避免resolver首失败掩盖后续物理角。
                            for (int g = 0; g < 10; ++g)
                            {
                        auto c = makeProbeConfig(cfg.corner_, static_cast<GateProbe>(g));
                                auto x = mark::fitObservedEdgePair(observed.contour, edges, c);
                                Json gate = {{"gate", gates[g]},
                                             {"success", bool(x.evidence)},
                                             {"reason", x.reason}};
                                if (x.evidence)
                                    gate.update(
                                        {{"intersection",
                                          {x.evidence->intersection_.x,
                                           x.evidence->intersection_.y}},
                                         {"line_mean_residual_px",
                                          x.evidence->line_mean_residual_px_},
                                         {"extension_px", x.evidence->support_extension_px_},
                                         {"corner_error_px", x.evidence->corner_error_px_}});
                                p["gates"].push_back(gate);
                            }
                        }
                        r["corners"].push_back(p);
                    }
                }
                out << r.dump() << '\n';
                std::cout << id << " completed=" << batch.hypotheses_.size() << '\n';
            }
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
