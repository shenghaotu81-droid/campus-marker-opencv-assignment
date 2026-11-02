#include "model/marker_geometry_loader.hpp"
#include "pipeline/decode_stage.hpp"
// 三L前半链路只读归因：所有阈值隔离/锚点枚举仅在工具内，不修改正式生成器。
#include "detector/config.hpp"
#include "preprocess/preprocess.hpp"
#include "geometry/geometry_observation.hpp"
#include "geometry/geometry_matcher.hpp"
#include "geometry/geometry_validation.hpp"
#include "common/file_digest.hpp"
#include <nlohmann/json.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
using Json = nlohmann::json;

namespace
{
    // V02：把组件身份接到观测/假设；ID 允许非连续，重复来源不能静默覆盖。
    std::map<size_t, const mark::WhiteComponent *>
    componentsById(const std::vector<mark::WhiteComponent> &components)
    {
        std::map<size_t, const mark::WhiteComponent *> result;
        for (const auto &component : components)
            if (!result.emplace(component.component_id_, &component).second)
                throw std::runtime_error("duplicate component identity in parent probe");
        return result;
    }

    // V02：修改诊断副本的锚点前按来源找位置，不要求观察序列与组件序列同序。
    std::map<size_t, size_t>
    observationPositions(const std::vector<mark::ShapeObservation> &observations)
    {
        std::map<size_t, size_t> result;
        for (size_t i = 0; i < observations.size(); ++i)
            if (!result.emplace(observations[i].source_component_id_, i).second)
                throw std::runtime_error("duplicate observation identity in parent probe");
        return result;
    }
}

// V02：只为首凹点对照拟合仿射，正式生成器仍枚举全部合法锚点。
    static cv::Mat fitFirstConcaveAnchorAffine(const std::vector<mark::ComponentAssignment> &assignment,
                                  const std::vector<mark::ShapeObservation> &observations,
                                  const mark::MarkerGeometry &model_geometry)
    {
        std::vector<cv::Point2f> model_points;

        std::vector<cv::Point2f> image_points;

        for (const auto &item : assignment)
        {

            auto model_it =
                std::find_if(model_geometry.polygons.begin(), model_geometry.polygons.end(),
                             [&](const mark::GeometryPolygon &polygon)
                             {
                                 return polygon.id == item.model_part_id_;
                             });

            if (model_it == model_geometry.polygons.end())
            {
                continue;
            }

            const auto *source = mark::findObservationById(observations,item.component_id_);
            if (!source)
                continue;
            const auto &observation = *source;

            if (!observation.anchor_vertex_index_.has_value())
            {
                continue;
            }

            const std::size_t vertex_index = observation.anchor_vertex_index_.value();

            if (vertex_index >= observation.simplified_polygon_.size())
            {
                continue;
            }

            model_points.push_back(model_it->anchor);

            image_points.push_back(observation.simplified_polygon_[vertex_index]);
        }


        if (model_points.size() < 3)
        {
            return cv::Mat();
        }


        return cv::estimateAffine2D(model_points, image_points);
    }


// 归档真实工作图多拓扑、原凹点与全部去重锚点；first_anchor仅作旧行为对照。
Json observationsJson(const std::vector<mark::WhiteComponent> &cs,
                      const std::vector<mark::ShapeObservation> &os)
{
    Json rows = Json::array();
    const auto components = componentsById(cs);
    std::set<size_t> sources;
    for (const auto &o : os)
    {
        if (!sources.insert(o.source_component_id_).second)
            throw std::runtime_error("duplicate observation identity in parent probe");
        const auto &component = *components.at(o.source_component_id_);
        Json concave = Json::array(), points = Json::array();
        for (auto t : o.turns_)
            if (t.type_ == mark::TurnType::CONCAVE)
                concave.push_back(t.vertex_index_);
        for (auto p : o.simplified_polygon_)
            points.push_back({p.x, p.y});
        Json topologies = Json::array(), anchors = Json::array();
        std::set<std::pair<float, float>> unique;
        for (auto &t : o.l_topology_candidates_)
        {
            Json polygon = Json::array();
            for (auto p : t.polygon_)
                polygon.push_back({p.x, p.y});
            topologies.push_back({{"epsilon", t.simplification_epsilon_},
                                  {"polygon", polygon},
                                  {"concave_indices", t.concave_vertex_indices_}});
            for (auto index : t.concave_vertex_indices_)
                if (index < t.polygon_.size())
                {
                    auto p = t.polygon_[index];
                    unique.emplace(p.x, p.y);
                }
        }
        if (std::find(o.supported_classes_.begin(), o.supported_classes_.end(), "L") !=
            o.supported_classes_.end())
            for (auto turn : o.turns_)
                if (turn.type_ == mark::TurnType::CONCAVE &&
                    turn.vertex_index_ < o.simplified_polygon_.size())
                {
                    auto p = o.simplified_polygon_[turn.vertex_index_];
                    unique.emplace(p.x, p.y);
                }
        for (auto p : unique)
            anchors.push_back({p.first, p.second});
        rows.push_back({{"component", component.component_id_},
                        {"source_component_id", o.source_component_id_},
                        {"area", component.area_},
                        {"border", component.touches_border_},
                        {"vertices", o.turns_.size()},
                        {"concave_indices", concave},
                        {"first_anchor",
                         o.anchor_vertex_index_ ? Json(*o.anchor_vertex_index_) : Json(nullptr)},
                        {"classes", o.supported_classes_},
                        {"polygon", points},
                        {"L_topology_candidates", topologies},
                        {"all_measured_L_anchors", anchors}});
    }
    return rows;
}

// 严格复刻冻结v2逐投影顶点到实测连续轮廓的均值，不用新增拓扑规则。
Json hypothesisMetrics(const mark::GeometryHypothesis &h, const mark::MarkerGeometry &m,
                       const std::vector<mark::WhiteComponent> &cs)
{
    Json a = Json::array();
    double worst = 0;
    const auto components = componentsById(cs);
    for (auto assign : h.assignments_)
    {
        auto it = std::find_if(m.polygons.begin(), m.polygons.end(),
                               [&](auto &p)
                               {
                                   return p.id == assign.model_part_id_;
                               });
        const auto &c = *components.at(assign.component_id_);
        std::vector<cv::Point2f> projected, contour;
        cv::transform(it->vertices, projected, h.affine_transform_);
        for (auto p : c.contour_)
            contour.emplace_back(p.x, p.y);
        double sum = 0;
        Json ds = Json::array();
        for (auto p : projected)
        {
            double d = std::abs(cv::pointPolygonTest(contour, p, true));
            sum += d;
            ds.push_back(d);
        }
        double residual = sum / projected.size();
        worst = std::max(worst, residual);
        a.push_back({{"part", assign.model_part_id_},
                     {"component", assign.component_id_},
                     {"residual", residual},
                     {"vertex_distances", ds},
                     {"area_ratio", std::abs(cv::contourArea(projected)) / c.area_},
                     {"border", c.touches_border_}});
    }
    auto &t = h.affine_transform_;
    return {{"assignments", a},
            {"worst_residual", worst},
            {"determinant",
             t.at<double>(0, 0) * t.at<double>(1, 1) - t.at<double>(0, 1) * t.at<double>(1, 0)}};
}

// 诊断首凹点假设：枚举同一候选L的已有凹点，模型/类剪枝/阈值均不变。
Json alternativeAnchors(const std::vector<mark::ShapeObservation> &os,
                        const mark::MarkerGeometry &m, const std::vector<mark::WhiteComponent> &cs,
                        const mark::GeometryConfig &cfg)
{
    bool truncated = false;
    const auto positions = observationPositions(os);
    auto combos = mark::generateThreeLAssignments(os, m, cfg, truncated);
    size_t tried = 0, passed = 0, proper = 0;
    double best = INFINITY;
    Json witness = nullptr, properWitness = nullptr;
    for (auto &as : combos)
    {
        std::vector<std::vector<size_t>> candidates;
        for (auto a : as)
        {
            std::vector<size_t> ids;
            for (auto t : os.at(positions.at(a.component_id_)).turns_)
                if (t.type_ == mark::TurnType::CONCAVE)
                    ids.push_back(t.vertex_index_);
            candidates.push_back(ids);
        }
        for (auto x : candidates[0])
            for (auto y : candidates[1])
                for (auto z : candidates[2])
                {
                    auto copy = os;
                    copy.at(positions.at(as[0].component_id_)).anchor_vertex_index_ = x;
                    copy.at(positions.at(as[1].component_id_)).anchor_vertex_index_ = y;
                    copy.at(positions.at(as[2].component_id_)).anchor_vertex_index_ = z;
                    auto transform = fitFirstConcaveAnchorAffine(as, copy, m);
                    if (transform.empty())
                        continue;
                    ++tried;
                    auto h = mark::buildGeometryHypothesis(as, transform);
                    auto metrics = hypothesisMetrics(h, m, cs);
                    double residual = metrics["worst_residual"];
                    if (residual < best)
                    {
                        best = residual;
                        witness = metrics;
                        witness["anchor_indices"] = {x, y, z};
                    }
                    if (residual <= cfg.max_validation_residual_)
                    {
                        ++passed;
                        if (metrics["determinant"].get<double>() > 0)
                        {
                            ++proper;
                            if (properWitness.is_null())
                            {
                                properWitness = metrics;
                                properWitness["anchor_indices"] = {x, y, z};
                            }
                        }
                    }
                }
    }
    return {{"diagnostic_only", true},
            {"tried", tried},
            {"passing", passed},
            {"positive_determinant_passing", proper},
            {"best_worst_residual", std::isfinite(best) ? Json(best) : Json(nullptr)},
            {"best_witness", witness},
            {"positive_determinant_witness", properWitness}};
}

// 全视频逐帧输出，失败原因互斥；孤立改变epsilon只作为设计适用条件诊断。
int main(int argc, char **argv)
{
    try
    {
        if (argc == 6 && std::string(argv[1]) == "--H-mask")
        {
            // 工具侧实例mask只确认组件真实身份；绝不传给正式观察、父生成或validator。
            auto cfg = mark::loadConfig(argv[4]).detector_config;
            auto model = mark::loadMarkerGeometry(cfg.marker_geometry_path_);
            std::filesystem::path fixtures = argv[3], path = argv[5];
            if (std::filesystem::exists(path))
                throw std::runtime_error("refusing overwrite");
            std::map<std::string, Json> truth;
            std::ifstream source(fixtures / "truth.jsonl");
            std::string line;
            while (std::getline(source, line))
            {
                auto t = Json::parse(line);
                truth[t["case_id"].get<std::string>()] = t;
            }
            std::ifstream in(argv[2]);
            std::ofstream out(path);
            size_t n = 0;
            while (std::getline(in, line))
            {
                auto row = Json::parse(line);
                auto t = truth.at(row["case_id"].get<std::string>());
                auto maskpath = fixtures / t["instances"].get<std::string>();
                if (audit::sha256(maskpath.string()) != t["instances_sha256"])
                    throw std::runtime_error("instance hash mismatch");
                auto mask = cv::imread(maskpath.string(), cv::IMREAD_UNCHANGED);
                if (mask.type() != CV_16UC1)
                    throw std::runtime_error("instance type mismatch");
                double scale = row["work_width"].get<double>() / mask.cols;
                Json components = Json::array();
                for (auto c : row["components"])
                {
                    std::set<int> labels;
                    for (auto p : c["contour"])
                    {
                        int x = cvRound((p[0].get<double>() + .5) / scale - .5),
                            y = cvRound((p[1].get<double>() + .5) / scale - .5);
                        if (x >= 0 && y >= 0 && x < mask.cols && y < mask.rows)
                        {
                            int label = mask.at<uint16_t>(y, x);
                            if (label)
                                labels.insert(label);
                        }
                    }
                    Json ids = Json::array();
                    for (auto label : labels)
                    {
                        if (label <= 0 || size_t(label) > model.polygons.size())
                            throw std::runtime_error("instance out of range");
                        ids.push_back(model.polygons[label - 1].id);
                    }
                    components.push_back({{"component", c["id"]},
                                          {"instance_labels", labels},
                                          {"true_parts", ids},
                                          {"unique_identity", labels.size() == 1}});
                }
                out << Json({{"case_id", row["case_id"]},
                             {"work_width", row["work_width"]},
                             {"components", components},
                             {"instances_sha256", t["instances_sha256"]},
                             {"tool_truth_only", true}})
                           .dump()
                    << '\n';
                ++n;
            }
            std::cout << "H mask rows=" << n << '\n';
            return n == 31 ? 0 : 1;
        }
        if (argc == 5 && std::string(argv[1]) == "--H-red")
        {
            // H失败日志保存了正式工作图原始轮廓；只读重放观察/父生成，不重新渲染或改变阈值。
            auto cfg = mark::loadConfig(argv[3]).detector_config;
            auto model = mark::loadMarkerGeometry(cfg.marker_geometry_path_);
            std::filesystem::path path = argv[4];
            if (std::filesystem::exists(path))
                throw std::runtime_error("refusing overwrite");
            std::ifstream in(argv[2]);
            std::ofstream out(path);
            std::string line;
            size_t n = 0;
            while (std::getline(in, line))
            {
                auto row = Json::parse(line);
                if (row["validated"].get<size_t>() != 0)
                    continue;
                std::vector<mark::WhiteComponent> cs;
                for (auto c : row["components"])
                {
                    mark::WhiteComponent component;
                    component.component_id_ = c["id"];
                    component.area_ = c["area"];
                    for (auto p : c["contour"])
                        component.contour_.emplace_back(p[0].get<int>(), p[1].get<int>());
                    component.bounding_box_ = cv::boundingRect(component.contour_);
                    cs.push_back(component);
                }
                auto os = mark::observeShapes(cs, cfg.geometry_);
                auto gen = mark::generateGeometryHypotheses(os, model, cfg.geometry_);
                auto val = mark::validateGeometryBatch(gen, model, cs, cfg.geometry_);
                Json r = {{"case_id", row["case_id"]},
                          {"work_width", row["work_width"]},
                          {"generated", gen.hypotheses_.size()},
                          {"validated", val.hypotheses_.size()},
                          {"observations", observationsJson(cs, os)},
                          {"alternative_anchors", alternativeAnchors(os, model, cs, cfg.geometry_)},
                          {"diagnostic_only", true}};
                for (auto &h : gen.hypotheses_)
                    r["hypotheses"].push_back(hypothesisMetrics(h, model, cs));
                out << r.dump() << '\n';
                ++n;
            }
            std::cout << "H replay rows=" << n << '\n';
            return n == 15 ? 0 : 1;
        }
        if (argc != 4)
            throw std::runtime_error("parent_diagnose <video> <frozen config> <new output dir>");
        std::filesystem::path dir = argv[3];
        if (std::filesystem::exists(dir))
            throw std::runtime_error("refusing overwrite");
        std::filesystem::create_directories(dir);
        auto cfg = mark::loadConfig(argv[2]).detector_config;
        auto model = mark::loadMarkerGeometry(cfg.marker_geometry_path_);
        cv::VideoCapture cap(argv[1]);
        if (!cap.isOpened())
            throw std::runtime_error("video open failed");
        Json summary = {{"video_sha256", audit::sha256(argv[1])},
                        {"config_sha256", audit::sha256(argv[2])},
                        {"no_truth_labels", true},
                        {"production_changes", false}};
        std::ofstream out(dir / "parents.jsonl");
        std::map<std::string, size_t> counts;
        size_t total = 0;
        cv::Mat image;
        for (int id = 0; cap.read(image); ++id)
        {
            mark::FrameInput input{};
            input.image = image;
            input.frame_id = id;
            auto steps = mark::runGeometrySteps(input, cfg, model, mark::GeometryStop::Validated);
            const auto &f = steps.prepared;
            const auto &cs = steps.components;
            const auto &os = steps.observations;
            const auto &gen = steps.generated;
            const auto &val = steps.validated;
            size_t ls = 0, anchors = 0;
            for (auto &o : os)
                if (std::find(o.supported_classes_.begin(), o.supported_classes_.end(), "L") !=
                    o.supported_classes_.end())
                {
                    ++ls;
                    if (o.anchor_vertex_index_)
                        ++anchors;
                }
            std::string cause = !val.hypotheses_.empty()  ? "has_validated_parent"
                                : cs.empty()              ? "no_white_components"
                                : ls < 3                  ? "fewer_than_three_L_observations"
                                : anchors < 3             ? "fewer_than_three_L_anchors"
                                : gen.hypotheses_.empty() ? "affine_generation_empty"
                                                          : "all_generated_rejected_by_residual";
            Json r = {{"frame_id", id},
                      {"components", cs.size()},
                      {"L_observations", ls},
                      {"L_anchors", anchors},
                      {"generated", gen.hypotheses_.size()},
                      {"validated", val.hypotheses_.size()},
                      {"cause", cause},
                      {"observations", observationsJson(cs, os)},
                      {"generation_diagnostics", gen.diagnostics_},
                      {"validation_diagnostics", val.diagnostics_},
                      {"truncated", gen.resource_truncated_}};
            double best = INFINITY;
            for (auto &h : gen.hypotheses_)
            {
                auto x = hypothesisMetrics(h, model, cs);
                best = std::min(best, x["worst_residual"].get<double>());
                r["hypotheses"].push_back(x);
            }
            r["best_worst_residual"] = std::isfinite(best) ? Json(best) : Json(nullptr);
            if (val.hypotheses_.empty() && !cs.empty())
            {
                r["alternative_anchors"] = alternativeAnchors(os, model, cs, cfg.geometry_);
                for (double e : {1., 1.5, 2.5, 3., 4.})
                {
                    auto diag = cfg.geometry_;
                    diag.approximation_epsilon_ = e;
                    auto alt = mark::observeShapes(cs, diag);
                    auto g = mark::generateGeometryHypotheses(alt, model, diag);
                    auto v = mark::validateGeometryBatch(g, model, cs, diag);
                    size_t n = 0;
                    for (auto &o : alt)
                        if (std::find(o.supported_classes_.begin(), o.supported_classes_.end(),
                                      "L") != o.supported_classes_.end())
                            ++n;
                    r["epsilon_diagnostic_only"].push_back({{"epsilon", e},
                                                            {"L_observations", n},
                                                            {"generated", g.hypotheses_.size()},
                                                            {"validated", v.hypotheses_.size()}});
                }
            }
            // 灰度/白像素说明是否在阈值处失去输入，不推断无标注帧的MARK存在性。
            cv::Mat originalGray, workGray, binary;
            cv::cvtColor(image, originalGray, cv::COLOR_BGR2GRAY);
            cv::cvtColor(f.image_, workGray, cv::COLOR_BGR2GRAY);
            double lo, hi;
            cv::minMaxLoc(originalGray, &lo, &hi);
            r["original_gray"] = {{"min", lo}, {"max", hi}, {"mean", cv::mean(originalGray)[0]}};
            cv::threshold(originalGray, binary, cfg.geometry_.white_threshold_, 255,
                          cv::THRESH_BINARY);
            r["original_white_pixels"] = cv::countNonZero(binary);
            cv::minMaxLoc(workGray, &lo, &hi);
            r["work_gray"] = {{"min", lo}, {"max", hi}, {"mean", cv::mean(workGray)[0]}};
            cv::threshold(workGray, binary, cfg.geometry_.white_threshold_, 255, cv::THRESH_BINARY);
            r["work_white_pixels"] = cv::countNonZero(binary);
            if (id == 4 || id == 34 || id == 37 || id == 60 || id == 128)
                cv::imwrite((dir / ("frame-" + std::to_string(id) + ".png")).string(), image);
            out << r.dump() << '\n';
            ++counts[cause];
            ++total;
        }
        summary["total"] = total;
        summary["causes"] = counts;
        std::ofstream(dir / "summary.json") << summary.dump(2) << '\n';
        std::cout << summary.dump(2) << '\n';
        if (total != 1676)
            throw std::runtime_error("video coverage mismatch");
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
