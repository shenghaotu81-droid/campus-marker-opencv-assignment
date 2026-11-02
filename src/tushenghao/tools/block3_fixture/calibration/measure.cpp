#include "model/marker_geometry_loader.hpp"
// 仅合成隔离测量：真值提供身份/变换，正式resolver不读真值；不应用待求误差上限。
// 关联规则是显式实验recipe，任何结构失败都留在分母；不冒称生产预算获批。
#include "synthetic_gen/core.hpp"
#include "detector/config.hpp"
#include "corners/measurement/corner_resolver.hpp"
#include "preprocess/preprocess.hpp"
#include "geometry/observation/geometry_observation.hpp"
#include "vision/math/observed_geometry.hpp"
#include "geometry/completion/assignment_match_metrics.hpp"
#include "geometry/parent/geometry_matcher.hpp"
#include "geometry/validation/geometry_validation.hpp"
#include "../common/support_pixel_count.hpp"
#include <map>
#include <opencv2/imgcodecs.hpp>
#include <fstream>
#include <iostream>
#include <set>

namespace
{
    // 固定关联recipe，不使用C误差尾部调门限。待求上限取有限double最大值以仅记录测量。
    mark::CornerConfig recipe()
    {
        mark::CornerConfig c;
        c.min_line_points_ = 3;
        c.max_line_fit_error_ = std::numeric_limits<double>::max();
        c.max_corner_error_ = std::numeric_limits<double>::max();
        c.approximation_epsilon_ = 1;
        c.min_intersection_angle_deg_ = 5;
        c.observation_budget_ =
            mark::CornerObservationBudget{90, 4, 5, 2, 12, std::numeric_limits<double>::max(), 5};
        return c;
    }

    // 用工具实例真值校核支持弧确属指定模型外边，不能把找错边混入有效误差统计。
    bool correctEdge(const mark::CornerEvidence &e, const mark::GeometryPolygon &part,
                     const cv::Mat &affine, const mark::PreparedFrame &frame)
    {
        std::vector<cv::Point2d> expected;
        for (auto p : part.vertices)
            expected.push_back(frame.workToOriginal(mark::observed::project(affine, p)));
        for (int a = 0; a < 2; ++a)
            for (auto p : e.original_support_arcs_[a])
            {
                int edge = e.model_edge_ids_[a];
                double desired = mark::observed::segmentDistance(
                    p, expected[edge], expected[(edge + 1) % expected.size()]);
                double best = mark::observed::boundaryDistance(p, expected);
                if (desired - best >
                    64 * std::numeric_limits<double>::epsilon() * std::max(1.0, desired))
                    return false;
            }
        return true;
    }
}

int main(int argc, char **argv)
{
    try
    {
        if (argc != 3)
            throw std::runtime_error("用法: block3_measure <C fixture目录> <新jsonl文件>");
        sg::fs::path dir = argv[1], output = argv[2];
        if (sg::fs::exists(output))
            throw std::runtime_error("refusing overwrite measurements");
        auto manifest = sg::read_json(dir / "manifest.json");
        if (manifest["profile"] != "C")
            throw std::runtime_error("C测量禁止读H");
        auto app = mark::loadConfig(DETECTOR_CONFIG);
        auto model = mark::loadMarkerGeometry(app.detector_config.marker_geometry_path_);
        if (manifest["model_yaml_sha256"] !=
            sg::hash_file(app.detector_config.marker_geometry_path_))
            throw std::runtime_error("model hash changed");
        std::ifstream source(dir / "truth.jsonl");
        std::ofstream out(output);
        std::string line;
        size_t total = 0, failures = 0;
        while (std::getline(source, line))
        {
            auto truth = sg::Json::parse(line);
            auto input_path = dir / truth["image"].get<std::string>(),
                 instance_path = dir / truth["instances"].get<std::string>();
            if (sg::hash_file(input_path) != truth["image_sha256"] ||
                sg::hash_file(instance_path) != truth["instances_sha256"])
                throw std::runtime_error("fixture image hash changed");
            mark::FrameInput input{};
            input.image = cv::imread(input_path.string());
            input.frame_id = total;
            auto instances = cv::imread(instance_path.string(), cv::IMREAD_UNCHANGED);
            // 原图真实阈值轮廓，只用实例mask在工具侧给来源身份；距离不按关联门限截断。
            cv::Mat gray, mask;
            cv::cvtColor(input.image, gray, cv::COLOR_BGR2GRAY);
            cv::threshold(gray, mask, 200, 255, cv::THRESH_BINARY);
            std::vector<std::vector<cv::Point>> original_contours;
            cv::findContours(mask, original_contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
            std::map<int, std::vector<cv::Point2d>> original_by_label;
            for (const auto &contour : original_contours)
            {
                std::set<int> labels;
                for (auto p : contour)
                {
                    auto label = instances.at<uint16_t>(p);
                    if (label)
                        labels.insert(label);
                }
                if (labels.size() == 1)
                    original_by_label[*labels.begin()] = {contour.begin(), contour.end()};
            }
            double angle = truth["rotation_deg"].get<double>() * CV_PI / 180, cs = std::cos(angle),
                   sn = std::sin(angle), cx = truth["center"][0], cy = truth["center"][1];
            for (auto size : {cv::Size(480, 360), cv::Size(960, 720), cv::Size(1440, 1080)})
            {
                ++total;
                auto pre = app.detector_config.preprocess;
                pre.work_width = size.width;
                pre.work_height = size.height;
                auto frame = mark::preprocess(input, pre);
                frame.components_ =
                    mark::extractWhiteComponents(frame, app.detector_config.geometry_);
                mark::GeometryHypothesis hypothesis;
                hypothesis.affine_transform_ =
                    (cv::Mat_<double>(2, 3) << 2 * cs * frame.scale_x_, -2 * sn * frame.scale_x_,
                     (cx - 80 * cs + 80 * sn + 0.5) * frame.scale_x_ - 0.5, 2 * sn * frame.scale_y_,
                     2 * cs * frame.scale_y_,
                     (cy - 80 * sn - 80 * cs + 0.5) * frame.scale_y_ - 0.5);
                auto record = truth;
                record["work_size"] = {size.width, size.height};
                record["measurement_case_id"] =
                    truth["case_id"].get<std::string>() + "-w" + std::to_string(size.width);
                record["mode"] = "ISOLATED_CALIBRATION_EXPERIMENT_RECIPE_V1";
                record["config_sha256"] = sg::hash_file(DETECTOR_CONFIG);
                record["code_sha256"] = MEASURE_CODE_HASH;
                record["recipe"] = {{"approximation_epsilon_original_px", 1},
                                    {"turn_trim_original_px", 2},
                                    {"max_turn_connection_original_px", 12},
                                    {"edge_position_original_px", 4},
                                    {"component_mapping_original_px", 5},
                                    {"min_span_original_px", 5},
                                    {"min_intersection_deg", 5},
                                    {"pending_error_upper_bounds_applied", false},
                                    {"recipe_user_approved", false}};
                bool identity_good = true;
                std::set<int> assigned;
                std::map<size_t, int> component_labels;
                for (const auto &component : frame.components_)
                {
                    std::set<int> labels;
                    for (auto p : component.contour_)
                    {
                        auto q = frame.workToOriginal(p);
                        int x = cvRound(q.x), y = cvRound(q.y);
                        if (x >= 0 && y >= 0 && x < instances.cols && y < instances.rows)
                        {
                            auto label = instances.at<uint16_t>(y, x);
                            if (label)
                                labels.insert(label);
                        }
                    }
                    if (labels.size() != 1 || *labels.begin() > model.polygons.size() ||
                        !assigned.insert(*labels.begin()).second)
                    {
                        identity_good = false;
                        break;
                    }
                    hypothesis.assignments_.push_back(
                        {model.polygons[*labels.begin() - 1].id, component.component_id_});
                    component_labels[component.component_id_] = *labels.begin();
                }
                if (!identity_good || assigned.size() != 6)
                {
                    record["success"] = false;
                    record["reason"] = "FIXTURE_IDENTITY_OR_STRUCTURE_FAILED";
                }
                else
                {
                    // 正确身份的M/S原始匹配指标，同正式补全用一个helper，不应用待求上限。
                    std::vector<double> areas, model_areas;
                    for (const auto &a : hypothesis.assignments_)
                        if (a.model_part_id_[0] == 'L')
                        {
                            for (const auto &c : frame.components_)
                                if (c.component_id_ == a.component_id_)
                                    areas.push_back(c.area_);
                            for (const auto &p : model.polygons)
                                if (p.id == a.model_part_id_)
                                    model_areas.push_back(p.area);
                        }
                    std::sort(areas.begin(), areas.end());
                    std::sort(model_areas.begin(), model_areas.end());
                    sg::Json assignments = sg::Json::array();
                    double mapping_error = 0;
                    for (const auto &c : frame.components_)
                    {
                        std::vector<cv::Point2d> mapped;
                        for (auto p : c.contour_)
                            mapped.push_back(frame.workToOriginal(p));
                        const auto &original =
                            original_by_label.at(component_labels.at(c.component_id_));
                        mapping_error =
                            std::max(mapping_error,
                                     std::max(mark::sampledBoundaryDistance(mapped, original, 1),
                                              mark::sampledBoundaryDistance(original, mapped, 1)));
                        const auto &part = model.polygons[component_labels.at(c.component_id_) - 1];
                        if (part.id[0] == 'L')
                            continue;
                        auto m = mark::measureAssignmentMatch(c, part, hypothesis.affine_transform_,
                                                              areas.at(1), model_areas.at(1), 3, 1);
                        assignments.push_back(
                            {{"part", part.id},
                             {"valid", m.valid},
                             {"topology_valid", m.topology_valid},
                             {"vertices", m.observed_vertices},
                             {"boundary_distance_work_px", m.boundary_distance},
                             {"direction_diff_deg", std::isfinite(m.direction_diff)
                                                        ? sg::Json(m.direction_diff)
                                                        : sg::Json(nullptr)},
                             {"relative_area_error", m.relative_area_error},
                             {"selected_epsilon_work_px", m.selected_epsilon}});
                    }
                    record["assignment_recipe"] = {
                        {"epsilon_cap_work_px", 3},
                        {"epsilon_fractions", {2.0 / 6, 3.0 / 6, 4.0 / 6, 5.0 / 6, 1}},
                        {"sample_step_work_px", 1}};
                    record["assignment_metrics"] = assignments;
                    record["component_mapping_original_px"] = mapping_error;
                    // C额外核查真实三L生产者的投影误差，不能用理想affine预算冒充上游消费预算。
                    auto observations =
                        mark::observeShapes(frame.components_, app.detector_config.geometry_);
                    auto parents = mark::validateGeometryBatch(
                        mark::generateGeometryHypotheses(observations, model,
                                                         app.detector_config.geometry_),
                        model, frame.components_, app.detector_config.geometry_);
                    std::vector<mark::GeometryHypothesis> correct_parents;
                    for (const auto &parent : parents.hypotheses_)
                    {
                        bool correct = mark::observed::validAffine(parent.affine_transform_);
                        for (const auto &a : parent.assignments_)
                            correct =
                                correct &&
                                a.model_part_id_ ==
                                    model.polygons[component_labels.at(a.component_id_) - 1].id;
                        if (correct)
                            correct_parents.push_back(parent);
                    }
                    record["producer_truth_assignment_count"] = correct_parents.size();
                    double producer_assignment_boundary = 0, producer_assignment_direction = 0;
                    for (const auto &parent : correct_parents)
                        for (const auto &component : frame.components_)
                        {
                            const auto &part =
                                model.polygons[component_labels.at(component.component_id_) - 1];
                            if (part.id[0] == 'L')
                                continue;
                            auto m = mark::measureAssignmentMatch(component, part,
                                                                  parent.affine_transform_,
                                                                  areas[1], model_areas[1], 3, 1);
                            producer_assignment_boundary =
                                std::max(producer_assignment_boundary, m.boundary_distance);
                            producer_assignment_direction =
                                std::max(producer_assignment_direction, m.direction_diff);
                        }
                    record["producer_assignment_boundary_work_px"] =
                        correct_parents.empty() ? sg::Json(nullptr)
                                                : sg::Json(producer_assignment_boundary);
                    record["producer_assignment_direction_deg"] =
                        correct_parents.empty() ? sg::Json(nullptr)
                                                : sg::Json(producer_assignment_direction);
                    auto result = mark::resolveObservedCorners(frame, hypothesis, model, recipe());
                    record["success"] = bool(result.measurement_);
                    record["reason"] = result.rejection_reason_;
                    if (result.measurement_)
                    {
                        sg::Json corners = sg::Json::array();
                        bool correct = true;
                        double producer_position = 0, producer_direction = 0;
                        for (int i = 0; i < 4; ++i)
                        {
                            const auto &e = result.measurement_->evidence_[i];
                            const char *ids[] = {"L0", "M1", "L2", "L3"};
                            const mark::GeometryPolygon *part = nullptr;
                            for (const auto &p : model.polygons)
                                if (p.id == ids[i])
                                    part = &p;
                            correct = correct &&
                                      correctEdge(e, *part, hypothesis.affine_transform_, frame);
                            auto expected =
                                cv::Point2d(double(truth["physical_corners_original"][i][0]),
                                            double(truth["physical_corners_original"][i][1]));
                            std::array<double, 2> direction_error, position_error{{0, 0}},
                                span{{0, 0}};
                            for (int a = 0; a < 2; ++a)
                            {
                                int edge = e.model_edge_ids_[a];
                                auto direction =
                                    frame.workToOriginal(mark::observed::project(
                                        hypothesis.affine_transform_,
                                        part->vertices[(edge + 1) % part->vertices.size()])) -
                                    frame.workToOriginal(mark::observed::project(
                                        hypothesis.affine_transform_, part->vertices[edge]));
                                auto fit = a == 0 ? e.line_a_ : e.line_b_;
                                direction_error[a] =
                                    mark::observed::angleDeg({fit[0], fit[1]}, direction);
                                auto start = frame.workToOriginal(mark::observed::project(
                                    hypothesis.affine_transform_, part->vertices[edge]));
                                double lo = INFINITY, hi = -INFINITY;
                                for (auto p : e.original_support_arcs_[a])
                                {
                                    position_error[a] = std::max(position_error[a],
                                                                 mark::observed::segmentDistance(
                                                                     p, start, start + direction));
                                    double t = p.dot(cv::Point2d(fit[0], fit[1]));
                                    lo = std::min(lo, t);
                                    hi = std::max(hi, t);
                                }
                                span[a] = hi - lo;
                                for (const auto &parent : correct_parents)
                                {
                                    auto predicted_start =
                                        frame.workToOriginal(mark::observed::project(
                                            parent.affine_transform_, part->vertices[edge]));
                                    auto predicted_end =
                                        frame.workToOriginal(mark::observed::project(
                                            parent.affine_transform_,
                                            part->vertices[(edge + 1) % part->vertices.size()]));
                                    producer_direction = std::max(
                                        producer_direction,
                                        mark::observed::angleDeg({fit[0], fit[1]},
                                                                 predicted_end - predicted_start));
                                    for (auto point : e.original_support_arcs_[a])
                                        producer_position =
                                            std::max(producer_position,
                                                     mark::observed::segmentDistance(
                                                         point, predicted_start, predicted_end));
                                }
                            }
                            double connection_length = 0;
                            for (size_t k = 1; k < e.original_turn_arc_.size(); ++k)
                                connection_length +=
                                    cv::norm(e.original_turn_arc_[k] - e.original_turn_arc_[k - 1]);
                            sg::Json supports = sg::Json::array();
                            for (const auto &arc : e.original_support_arcs_)
                                supports.push_back(sg::points_json(arc));
                            corners.push_back(
                                {{"physical", i},
                                 {"kind", i == 1 ? "M" : "L"},
                                 {"evidence_id", e.stable_id_},
                                 {"intersection", {e.intersection_.x, e.intersection_.y}},
                                 {"line_mean_residual_px", e.line_mean_residual_px_},
                                 {"line_max_residual_px", e.line_max_residual_px_},
                                 {"support_extension_px", e.support_extension_px_},
                                 {"corner_error_px", e.corner_error_px_},
                                 {"direction_diff_deg", direction_error},
                                 {"truth_error_px", cv::norm(e.intersection_ - expected)},
                                 {"edge_position_original_px", position_error},
                                 {"support_span_original_px", span},
                                 {"turn_connection_length_original_px", connection_length},
                                 {"intersection_angle_deg",
                                  mark::observed::angleDeg({e.line_a_[0], e.line_a_[1]},
                                                           {e.line_b_[0], e.line_b_[1]})},
                                 // 独立像素数与生产最少点数一致；访问数单独保留，拟合仍用完整弧。
                                 {"support_points",
                                  {block3_fixture_tools::count_unique_support_pixels(
                                       e.original_support_arcs_[0]),
                                   block3_fixture_tools::count_unique_support_pixels(
                                       e.original_support_arcs_[1])}},
                                 {"support_visits",
                                  {e.original_support_arcs_[0].size(),
                                   e.original_support_arcs_[1].size()}},
                                 {"support_points_counting", "unique_pixel_coordinates"},
                                 {"support_arcs", supports},
                                 {"turn_arc", sg::points_json(e.original_turn_arc_)}});
                        }
                        record["corners"] = corners;
                        record["producer_edge_position_original_px"] =
                            correct_parents.empty() ? sg::Json(nullptr)
                                                    : sg::Json(producer_position);
                        record["producer_edge_direction_deg"] = correct_parents.empty()
                                                                    ? sg::Json(nullptr)
                                                                    : sg::Json(producer_direction);
                        if (!correct)
                        {
                            record["success"] = false;
                            record["reason"] = "WRONG_MODEL_EDGE_SUPPORT";
                        }
                    }
                }
                if (!record["success"].get<bool>())
                    ++failures;
                out << sg::canonical(record) << '\n';
            }
        }
        std::cout << "samples=" << total << " structure_failures=" << failures
                  << " output=" << output << '\n';
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
