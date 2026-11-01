// 输入冻结配置和H图像；先运行正式pipeline，再读取工具真值核查，不向检测传真值。
// 同时隔离定位以区分原三L/补全问题；输出逐样例、失败分母与证据，不用H定门限。
#include "synthetic_gen/core.hpp"
#include "config/config.hpp"
#include "pipeline/decode_stage.hpp"
#include "preprocess/preprocess.hpp"
#include "geometry/geometry_observation.hpp"
#include "corners/corner_resolver.hpp"
#include "corners/corner_observation.hpp"
#include "core/observed_geometry_utils.hpp"
#include "geometry/assignment_match_metrics.hpp"
#include "geometry/geometry_matcher.hpp"
#include "geometry/geometry_validation.hpp"
#include <opencv2/imgcodecs.hpp>
#include <fstream>
#include <iostream>
#include <set>
#include <chrono>
#include <map>

namespace
{
    // 保存每角原始支持，阶段空也保留diagnostics；不可用真值误差不会写成零。
    sg::Json evidence(const mark::CornerMeasurement &m)
    {
        sg::Json result = sg::Json::array();
        for (const auto &e : m.evidence_)
        {
            sg::Json arcs = sg::Json::array();
            for (const auto &arc : e.original_support_arcs_)
                arcs.push_back(sg::points_json(arc));
            result.push_back({{"id", e.stable_id_},
                              {"frame_id", e.frame_id_},
                              {"component_id", e.component_id_},
                              {"model_edges", e.model_edge_ids_},
                              {"segment_starts", e.observed_segment_ids_},
                              {"segment_ends", e.observed_segment_end_ids_},
                              {"fitted_lines",
                               {{e.line_a_[0], e.line_a_[1], e.line_a_[2], e.line_a_[3]},
                                {e.line_b_[0], e.line_b_[1], e.line_b_[2], e.line_b_[3]}}},
                              {"intersection", {e.intersection_.x, e.intersection_.y}},
                              {"support_arcs", arcs},
                              {"turn_arc", sg::points_json(e.original_turn_arc_)},
                              {"line_mean_residual_px", e.line_mean_residual_px_},
                              {"line_max_residual_px", e.line_max_residual_px_},
                              {"extension_px", e.support_extension_px_},
                              {"corner_error_px", e.corner_error_px_}});
        }
        return result;
    }

    // 工具侧唯一已知身份入口；只用于隔离诊断，不给runDecodePipeline传真值。
    mark::GeometryHypothesis isolatedHypothesis(const mark::PreparedFrame &frame,
                                                const mark::MarkerGeometry &model,
                                                const sg::Json &truth, const cv::Mat &instances,
                                                bool &identity)
    {
        mark::GeometryHypothesis hypothesis;
        double angle = double(truth["rotation_deg"]) * CV_PI / 180, cs = std::cos(angle),
               sn = std::sin(angle), cx = truth["center"][0], cy = truth["center"][1];
        hypothesis.affine_transform_ =
            (cv::Mat_<double>(2, 3) << 2 * cs * frame.scale_x_, -2 * sn * frame.scale_x_,
             (cx - 80 * cs + 80 * sn + 0.5) * frame.scale_x_ - 0.5, 2 * sn * frame.scale_y_,
             2 * cs * frame.scale_y_, (cy - 80 * sn - 80 * cs + 0.5) * frame.scale_y_ - 0.5);
        identity = true;
        std::set<int> assigned;
        for (const auto &c : frame.components_)
        {
            std::set<int> labels;
            for (auto p : c.contour_)
            {
                auto q = frame.workToOriginal(p);
                int x = cvRound(q.x), y = cvRound(q.y);
                if (x >= 0 && y >= 0 && x < instances.cols && y < instances.rows)
                {
                    int n = instances.at<uint16_t>(y, x);
                    if (n)
                        labels.insert(n);
                }
            }
            if (labels.size() != 1 || *labels.begin() > model.polygons.size() ||
                !assigned.insert(*labels.begin()).second)
            {
                identity = false;
                break;
            }
            hypothesis.assignments_.push_back(
                {model.polygons[*labels.begin() - 1].id, c.component_id_});
        }
        identity = identity && assigned.size() == 6;
        return hypothesis;
    }

}

int main(int argc, char **argv)
{
    try
    {
        // 失败链路只读诊断：保持冻结预算，逐父/组件记录原始匹配指标。
        // 原来仅有空分支原因，无法区分旧三L投影偏差与拓扑/面积拒绝；不据H重定门限。
        if (argc == 6 && std::string(argv[1]) == "--diagnose-stage")
        {
            sg::fs::path dir = argv[2];
            std::ifstream source(dir / "truth.jsonl");
            std::string line;
            sg::Json fixture;
            while (std::getline(source, line))
            {
                auto row = sg::Json::parse(line);
                if (row["case_id"] == argv[3])
                {
                    fixture = row;
                    break;
                }
            }
            if (fixture.is_null())
                throw std::runtime_error("diagnostic case missing");
            auto config = mark::loadConfig(argv[4]).detector_config;
            auto model = mark::loadMarkerGeometry(config.marker_geometry_path_);
            config.preprocess.work_width = std::stoi(argv[5]);
            config.preprocess.work_height = config.preprocess.work_width * 3 / 4;
            if (!config.assignment_completion_ ||
                (config.preprocess.work_width != 480 && config.preprocess.work_width != 960 &&
                 config.preprocess.work_width != 1440))
                throw std::runtime_error(
                    "diagnostic requires explicit assignment budget and fixed work width");
            mark::FrameInput input{};
            input.image = cv::imread((dir / fixture["image"].get<std::string>()).string());
            auto steps = mark::runGeometrySteps(input, config, model, mark::GeometryStop::Validated);
                auto &frame = steps.prepared;
                const auto &generated = steps.generated;
                const auto &parents = steps.validated;
                // 旧单步探针入口继续使用兼容载体；主流程不借此回填共享状态。
                frame.components_ = steps.components;
            sg::Json row = {{"case_id", fixture["case_id"]},
                            {"work_width", config.preprocess.work_width},
                            {"diagnostic_only", true},
                            {"config_sha256", sg::hash_file(argv[4])},
                            {"code_sha256", MEASURE_CODE_HASH},
                            {"generated_parents", generated.hypotheses_.size()},
                            {"validated_parents", parents.hypotheses_.size()},
                            {"generation_diagnostics", generated.diagnostics_},
                            {"validation_diagnostics", parents.diagnostics_}};
            const auto &budget = *config.assignment_completion_;
            for (size_t i = 0; i < parents.hypotheses_.size(); ++i)
            {
                const auto &parent = parents.hypotheses_[i];
                if (!mark::observed::validAffine(parent.affine_transform_))
                    continue;
                std::set<size_t> used;
                std::vector<double> areas, model_areas;
                for (const auto &a : parent.assignments_)
                {
                    used.insert(a.component_id_);
                    for (const auto &c : frame.components_)
                        if (c.component_id_ == a.component_id_)
                            areas.push_back(c.area_);
                    for (const auto &p : model.polygons)
                        if (p.id == a.model_part_id_)
                            model_areas.push_back(p.area);
                }
                if (areas.size() != 3)
                    continue;
                std::sort(areas.begin(), areas.end());
                std::sort(model_areas.begin(), model_areas.end());
                for (const auto &p : model.polygons)
                    if (p.id[0] != 'L')
                        for (const auto &c : frame.components_)
                            if (!used.count(c.component_id_))
                            {
                                auto m = mark::measureAssignmentMatch(
                                    c, p, parent.affine_transform_, areas[1], model_areas[1],
                                    budget.approximation_epsilon_work_px,
                                    budget.boundary_sample_step_work_px);
                                row["metrics"].push_back(
                                    {{"parent", i},
                                     {"part", p.id},
                                     {"component", c.component_id_},
                                     {"valid", m.valid},
                                     {"topology", m.topology_valid},
                                     {"boundary_work_px", m.boundary_distance},
                                     {"direction_deg", std::isfinite(m.direction_diff)
                                                           ? sg::Json(m.direction_diff)
                                                           : sg::Json(nullptr)},
                                     {"relative_area_error", m.relative_area_error},
                                     {"epsilon_work_px", m.selected_epsilon}});
                            }
            }
            std::cout << sg::canonical(row) << '\n';
            return 0;
        }
        // 单失败例逐项关闭门限仅定位拒绝原因；不产生新预算、不用于H验收或生产。
        if (argc == 5 && std::string(argv[1]) == "--diagnose-one")
        {
            sg::fs::path dir = argv[2];
            std::ifstream source(dir / "truth.jsonl");
            std::string line;
            sg::Json truth;
            while (std::getline(source, line))
            {
                auto row = sg::Json::parse(line);
                if (row["case_id"] == argv[3])
                {
                    truth = row;
                    break;
                }
            }
            if (truth.is_null())
                throw std::runtime_error("diagnostic case missing");
            auto config = mark::loadConfig(argv[4]).detector_config;
            auto model = mark::loadMarkerGeometry(config.marker_geometry_path_);
            config.preprocess.work_width = 1440;
            config.preprocess.work_height = 1080;
            mark::FrameInput input{};
            input.image = cv::imread((dir / truth["image"].get<std::string>()).string());
            auto frame = mark::preprocess(input, config.preprocess);
            frame.components_ = mark::extractWhiteComponents(frame, config.geometry_);
            bool identity = false;
            auto instances = cv::imread((dir / truth["instances"].get<std::string>()).string(),
                                        cv::IMREAD_UNCHANGED);
            auto hypothesis = isolatedHypothesis(frame, model, truth, instances, identity);
            if (!identity)
                throw std::runtime_error("diagnostic identity invalid");
            const char *names[] = {
                "baseline",           "direction_gate_off", "position_gate_off",
                "span_gate_off",      "points_gate_off",    "residual_gate_off",
                "extension_gate_off", "corner_gate_off",    "connection_gate_off"};
            for (int gate = 0; gate < 9; ++gate)
            {
                auto c = config.corner_;
                auto &budget = *c.observation_budget_;
                const double unlimited = std::numeric_limits<double>::max();
                if (gate == 1)
                    budget.max_edge_direction_diff_deg = 90;
                if (gate == 2)
                    budget.max_edge_position_distance_px = unlimited;
                if (gate == 3)
                    budget.min_support_span_px = 0.001;
                if (gate == 4)
                    c.min_line_points_ = 3;
                if (gate == 5)
                    c.max_line_fit_error_ = unlimited;
                if (gate == 6)
                    budget.max_support_extension_px = unlimited;
                if (gate == 7)
                    c.max_corner_error_ = unlimited;
                if (gate == 8)
                    budget.max_turn_connection_length_px = unlimited;
                auto result = mark::resolveObservedCorners(frame, hypothesis, model, c);
                sg::Json row = {{"case_id", truth["case_id"]},
                                {"diagnostic_only", true},
                                {"gate", names[gate]},
                                {"success", bool(result.measurement_)},
                                {"reason", result.rejection_reason_}};
                if (gate == 0)
                {
                    for (const auto &assignment : hypothesis.assignments_)
                        if (assignment.model_part_id_ == "L3")
                            for (const auto &component : frame.components_)
                                if (component.component_id_ == assignment.component_id_)
                                {
                                    auto contour = mark::observeOriginalContour(
                                        frame, component, *c.observation_budget_);
                                    row["regression_contour"] = sg::points_json(
                                        {contour.contour.begin(), contour.contour.end()});
                                }
                    for (const auto &part : model.polygons)
                        if (part.id == "L3")
                        {
                            sg::Json points = sg::Json::array();
                            for (int vertex : {5, 0, 0, 1})
                            {
                                auto p = frame.workToOriginal(mark::observed::project(
                                    hypothesis.affine_transform_, part.vertices[vertex]));
                                points.push_back({p.x, p.y});
                            }
                            row["regression_edges"] = points;
                        }
                    row["regression_expected"] = truth["physical_corners_original"][3];
                }
                if (result.measurement_)
                    row["evidence"] = evidence(*result.measurement_);
                std::cout << sg::canonical(row) << '\n';
            }
            return 0;
        }
        // 可review候选先由强类型导出，只写新文件，不改生产配置；审批在调用方记录。
        if (argc == 3 && std::string(argv[1]) == "--candidate-config")
        {
            if (sg::fs::exists(argv[2]))
                throw std::runtime_error("refusing overwrite candidate config");
            auto app = mark::loadConfig(DETECTOR_CONFIG);
            auto &c = app.detector_config.corner_;
            c.min_line_points_ = 10;
            c.max_line_fit_error_ = 0.5;
            c.max_corner_error_ = 1.5;
            c.approximation_epsilon_ = 1;
            c.min_intersection_angle_deg_ = 5;
            c.observation_budget_ = mark::CornerObservationBudget{1, 2, 4.5, 2, 13.5, 9.5, 14};
            app.detector_config.assignment_completion_ =
                mark::AssignmentCompletionConfig{3.5, 3, 30, 0.045, 1, 128, 4096, 64};
            mark::writeEffectiveConfig(app, argv[2]);
            std::cout << "CANDIDATE_ONLY " << argv[2] << '\n';
            return 0;
        }
        if (argc != 4 && argc != 5)
            throw std::runtime_error(
                "用法: block3_verify <C/H目录> <冻结配置> <新jsonl> [C诊断原图上限]");
        sg::fs::path dir = argv[1], path = argv[3];
        if (sg::fs::exists(path))
            throw std::runtime_error("refusing overwrite");
        auto manifest = sg::read_json(dir / "manifest.json");
        if (manifest["profile"] != "H" && manifest["profile"] != "C")
            throw std::runtime_error("verification requires fixed C/H");
        int limit = argc == 5 ? std::stoi(argv[4]) : 240;
        if (limit <= 0 || limit > 240 || (manifest["profile"] == "H" && limit != 240))
            throw std::runtime_error("H必须全720，C诊断上限1..240");
        auto app = mark::loadConfig(argv[2]);
        auto config = app.detector_config;
        auto model = mark::loadMarkerGeometry(config.marker_geometry_path_);
        if (!config.assignment_completion_ || !config.corner_.observation_budget_)
            throw std::runtime_error("explicit full budget required");
        if (manifest["model_yaml_sha256"] != sg::hash_file(config.marker_geometry_path_))
            throw std::runtime_error("model changed");
        auto config_hash = sg::hash_file(argv[2]);
        std::ifstream source(dir / "truth.jsonl");
        std::ofstream out(path);
        std::string line;
        size_t total = 0, correct = 0, isolated_correct = 0, wrong = 0, noiseless_correct = 0;
        std::map<std::pair<int, int>, size_t> noisy_correct;
        while (total < static_cast<size_t>(limit * 3) && std::getline(source, line))
        {
            auto truth = sg::Json::parse(line);
            auto image_path = dir / truth["image"].get<std::string>();
            if (sg::hash_file(image_path) != truth["image_sha256"])
                throw std::runtime_error("image hash changed");
            mark::FrameInput input{};
            input.image = cv::imread(image_path.string());
            for (auto size : {cv::Size(480, 360), cv::Size(960, 720), cv::Size(1440, 1080)})
            {
                input.frame_id = total++;
                input.timestamp_us = input.frame_id * 10000;
                config.preprocess.work_width = size.width;
                config.preprocess.work_height = size.height;
                auto begin = std::chrono::steady_clock::now();
                auto stage = mark::runDecodePipeline(input, config, model);
                double elapsed = std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - begin)
                                     .count();
                // 正式pipeline已返回；真值只参与以下工具评价。
                std::array<cv::Point2d, 4> expected{};
                for (int i = 0; i < 4; ++i)
                    expected[i] = {double(truth["physical_corners_original"][i][0]),
                                   double(truth["physical_corners_original"][i][1])};
                bool good = stage.detections.size() == 1, wrong_output = false;
                double worst = 0;
                sg::Json detections = sg::Json::array();
                for (const auto &d : stage.detections)
                {
                    sg::Json points = sg::Json::array();
                    for (auto p : d.corners)
                        points.push_back({p.x, p.y});
                    double error = 0;
                    bool known = bool(d.attributes.orientation);
                    if (known)
                        for (int i = 0; i < 4; ++i)
                        {
                            int index = (*d.attributes.orientation)[i];
                            if (index < 0 || index > 3)
                            {
                                known = false;
                                break;
                            }
                            error = std::max(error,
                                             cv::norm(cv::Point2d(d.corners[index]) - expected[i]));
                        }
                    // unknown完整H不算正确唯一；非空方向与物理真值不符属于错误有效输出。
                    good = good && known && error <= 2.0;
                    wrong_output = wrong_output || (known && error > 2.0);
                    worst = std::max(worst, error);
                    detections.push_back(
                        {{"corners", points},
                         {"orientation",
                          known ? sg::Json(*d.attributes.orientation) : sg::Json(nullptr)},
                         {"bbox", {d.bbox.x, d.bbox.y, d.bbox.width, d.bbox.height}},
                         {"marker_code", nullptr},
                         {"confidence", nullptr},
                         {"truth_error_px", known ? sg::Json(error) : sg::Json(nullptr)}});
                }
                auto frame = mark::preprocess(input, config.preprocess);
                frame.components_ = mark::extractWhiteComponents(frame, config.geometry_);
                auto instances_path = dir / truth["instances"].get<std::string>();
                if (sg::hash_file(instances_path) != truth["instances_sha256"])
                    throw std::runtime_error("instance hash changed");
                auto instances = cv::imread(instances_path.string(), cv::IMREAD_UNCHANGED);
                bool identity = false;
                auto hypothesis = isolatedHypothesis(frame, model, truth, instances, identity);
                mark::CornerResolution isolated;
                if (identity)
                    isolated =
                        mark::resolveObservedCorners(frame, hypothesis, model, config.corner_);
                double isolated_error = INFINITY;
                if (isolated.measurement_)
                {
                    isolated_error = 0;
                    for (int i = 0; i < 4; ++i)
                        isolated_error = std::max(
                            isolated_error,
                            cv::norm(isolated.measurement_->physical_corners_[i] - expected[i]));
                }
                bool isolated_good = isolated.measurement_ && isolated_error <= 2.0;
                sg::Json measurements = sg::Json::array();
                for (const auto &m : stage.measurements)
                    measurements.push_back(evidence(m));
                auto record = truth;
                record["work_size"] = {size.width, size.height};
                record["case_work_id"] =
                    truth["case_id"].get<std::string>() + "-w" + std::to_string(size.width);
                record["config_sha256"] = config_hash;
                record["code_sha256"] = MEASURE_CODE_HASH;
                record["status"] = int(stage.status);
                record["mode"] = manifest["profile"] == "C" ? "C_PIPELINE_DIAGNOSTIC"
                                                            : "H_FROZEN_CONFIG_VERIFICATION";
                record["stage_correct"] = good;
                record["wrong_valid_output"] = wrong_output;
                record["stage_ms"] = elapsed;
                record["detections"] = detections;
                record["diagnostics"] = stage.diagnostics;
                record["search_truncated"] = stage.search_truncated;
                record["measurements"] = measurements;
                record["isolated_correct"] = isolated_good;
                record["isolated_reason"] = isolated.rejection_reason_;
                record["isolated_truth_error_px"] =
                    isolated.measurement_ ? sg::Json(isolated_error) : sg::Json(nullptr);
                if (isolated.measurement_)
                    record["isolated_evidence"] = evidence(*isolated.measurement_);
                out << sg::canonical(record) << '\n';
                correct += good;
                isolated_correct += isolated_good;
                wrong += wrong_output;
                if (truth["seed"] == 0)
                    noiseless_correct += good;
                else
                    noisy_correct[{size.width, truth["radius_original_px"].get<int>()}] += good;
            }
        }
        bool coverage = total == static_cast<size_t>(limit * 3);
        bool accepted = coverage && noiseless_correct == 144 &&
                        correct - noiseless_correct >= 571 && wrong == 0;
        for (const auto &group : noisy_correct)
            accepted = accepted && group.second >= 95;
        std::cout << "total=" << total << " stage_correct=" << correct
                  << " isolated_correct=" << isolated_correct << " wrong_valid=" << wrong
                  << " acceptance="
                  << (manifest["profile"] == "H" ? (accepted ? "PASS" : "FAIL") : "C_DIAGNOSTIC")
                  << '\n';
        return !coverage ? 1 : manifest["profile"] == "H" && !accepted ? 2 : 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
