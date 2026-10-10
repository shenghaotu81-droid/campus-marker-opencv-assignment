#include "geometry/geometry_matcher.hpp"
#include "geometry/geometry_observation.hpp"
#include "geometry/geometry_validation.hpp"
#include "observability_fixture.hpp"
#include <iostream>
#include <map>
#include <set>
#include <limits>
#include <iomanip>
using temporal_fixture::check;

namespace
{
    // 原 fixture 的锚点共线、模型身份错误且没有真实拓扑；答案仅用于测试侧筛选和事后核对。
    void real_three_l()
    {
        auto scene = fixture::scene();
        auto config = observability_fixture::app().detector_config.geometry_;
        auto model = fixture::model();
        std::map<std::string, std::size_t> truth;
        for (const auto &a : scene.hypothesis.assignments_)
            if (a.model_part_id_ == "L0" || a.model_part_id_ == "L2" || a.model_part_id_ == "L3")
                truth.emplace(a.model_part_id_, a.component_id_);
        std::vector<mark::WhiteComponent> components;
        for (const auto &c : scene.frame.components_)
            for (const auto &a : truth)
                if (c.component_id_ == a.second)
                    components.push_back(c);
        check(components.size() == 3, "three actual L components missing");
        auto observations = mark::observeShapes(components, config);
        auto batch = mark::generateGeometryHypotheses(observations, model, config);
        check(!batch.hypotheses_.empty(), "real nondegenerate three-L fixture rejected");
        bool found = false;
        for (const auto &h : batch.hypotheses_)
        {
            check(h.assignments_.size() == 3, "three assignments required");
            check(h.validation_residual_ == -1.0, "generated residual must be unmeasured");
            std::set<std::size_t> used;
            std::map<std::string, std::size_t> mapping;
            for (const auto &a : h.assignments_)
            {
                check(used.insert(a.component_id_).second, "duplicate component assignment");
                check(std::any_of(components.begin(), components.end(),
                                  [&](const auto &c)
                                  {
                                      return c.component_id_ == a.component_id_;
                                  }),
                      "missing assigned component");
                mapping.emplace(a.model_part_id_, a.component_id_);
            }
            check(h.affine_transform_.rows == 2 && h.affine_transform_.cols == 3 &&
                      cv::checkRange(h.affine_transform_),
                  "finite affine required");
            found = found || mapping == truth;
        }
        check(found, "true model correspondence absent");
        auto limited = config;
        limited.max_hypothesis_count_ = 1;
        check(mark::generateGeometryHypotheses(observations, model, limited).resource_truncated_,
              "resource truncation flag lost");
        // 保留每个合法 L 的像素轮廓，只将实测凹锚点平移到同一直线上。
        for (std::size_t i = 0; i < components.size(); ++i)
        {
            auto it = std::find_if(observations.begin(), observations.end(),
                                   [&](const auto &o)
                                   {
                                       return o.source_component_id_ == components[i].component_id_;
                                   });
            check(it != observations.end() && it->anchor_vertex_index_, "real anchor missing");
            auto anchor = it->simplified_polygon_.at(*it->anchor_vertex_index_);
            cv::Point delta(cvRound(200 + 200 * i - anchor.x), cvRound(300 - anchor.y));
            for (auto &point : components[i].contour_)
                point += delta;
            components[i].bounding_box_ = cv::boundingRect(components[i].contour_);
        }
        auto degenerate = mark::generateGeometryHypotheses(mark::observeShapes(components, config),
                                                           model, config);
        check(degenerate.hypotheses_.empty(), "collinear anchors accepted");
    }

    // 方形仅用于已冻结距离公式的可解析测试，不替换生产 MARK/L 拓扑 fixture。
    struct ValidationScene
    {
        mark::MarkerGeometry model;
        std::vector<mark::WhiteComponent> components;
        mark::GeometryBatch batch;
        mark::GeometryConfig config;
    };

    ValidationScene validationScene(const std::vector<int> &expansions)
    {
        ValidationScene scene;
        scene.config.max_validation_residual_ = 5.0;
        scene.config.min_area_ratio_ = 0.1;
        scene.config.max_area_ratio_ = 10.0;
        mark::GeometryHypothesis hypothesis;
        hypothesis.affine_transform_ = (cv::Mat_<double>(2, 3) << 1, 0, 0, 0, 1, 0);
        hypothesis.validation_residual_ = -1.0;
        const std::array<std::size_t, 3> ids{{11, 42, 105}};
        const std::array<std::string, 3> names{{"L0", "L2", "L3"}};
        for (std::size_t i = 0; i < expansions.size(); ++i)
        {
            int x = 10 + static_cast<int>(i) * 40, y = 10, margin = expansions[i];
            mark::GeometryPolygon part;
            part.id = names.at(i);
            part.vertices = {{float(x), float(y)},
                             {float(x + 10), float(y)},
                             {float(x + 10), float(y + 10)},
                             {float(x), float(y + 10)}};
            part.area = 100;
            scene.model.polygons.push_back(part);
            mark::WhiteComponent component;
            component.component_id_ = ids.at(i);
            component.contour_ = {{x - margin, y - margin},
                                  {x + 10 + margin, y - margin},
                                  {x + 10 + margin, y + 10 + margin},
                                  {x - margin, y + 10 + margin}};
            component.area_ = cv::contourArea(component.contour_);
            scene.components.push_back(component);
            hypothesis.assignments_.push_back({part.id, component.component_id_});
        }
        scene.batch.hypotheses_.push_back(hypothesis);
        return scene;
    }

    // 从模型和观测独立算各部件均值，既查解析预期，也输出可归档的逐项对照。
    mark::GeometryBatch verifyResidual(const ValidationScene &scene, double expected,
                                       const char *name)
    {
        auto output =
            mark::validateGeometryBatch(scene.batch, scene.model, scene.components, scene.config);
        check(output.hypotheses_.size() == 1, std::string(name) + " valid fixture rejected");
        double maximum = 0;
        std::cout << std::setprecision(17) << "GEOMETRY_MEASUREMENT {\"case\":\"" << name
                  << "\",\"part_means\":[";
        std::size_t index = 0;
        for (const auto &assignment : scene.batch.hypotheses_[0].assignments_)
        {
            auto part = std::find_if(scene.model.polygons.begin(), scene.model.polygons.end(),
                                     [&](const auto &p)
                                     {
                                         return p.id == assignment.model_part_id_;
                                     });
            auto component = std::find_if(scene.components.begin(), scene.components.end(),
                                          [&](const auto &c)
                                          {
                                              return c.component_id_ == assignment.component_id_;
                                          });
            check(part != scene.model.polygons.end() && component != scene.components.end(),
                  "independent identity missing");
            std::vector<cv::Point2f> projected, contour;
            cv::transform(part->vertices, projected, scene.batch.hypotheses_[0].affine_transform_);
            for (auto point : component->contour_)
                contour.emplace_back(point);
            double total = 0;
            for (auto point : projected)
                total += std::abs(cv::pointPolygonTest(contour, point, true));
            double mean = total / projected.size();
            maximum = std::max(maximum, mean);
            if (index++)
                std::cout << ',';
            std::cout << mean;
        }
        double measured = output.hypotheses_[0].validation_residual_;
        std::cout << "],\"independent_max\":" << maximum << ",\"field\":" << measured << "}\n";
        check(std::isfinite(measured) && std::abs(measured - expected) <= 1e-12 &&
                  std::abs(measured - maximum) <= 1e-12,
              std::string(name) + " residual statistic mismatch");
        return output;
    }

    void residualZero()
    {
        verifyResidual(validationScene({0}), 0, "G01");
    }

    void residualOne()
    {
        verifyResidual(validationScene({1}), 1, "G02");
    }

    void residualMaximum()
    {
        verifyResidual(validationScene({0, 1, 3}), 3, "G03");
    }

    void residualBoundary()
    {
        auto scene = validationScene({1});
        scene.config.max_validation_residual_ = 1;
        verifyResidual(scene, 1, "G04-equal");
        scene.config.max_validation_residual_ = std::nextafter(1.0, 0.0);
        check(mark::validateGeometryBatch(scene.batch, scene.model, scene.components, scene.config)
                  .hypotheses_.empty(),
              "G04 next smaller threshold accepted");
    }

    void residualInputUnchanged()
    {
        auto scene = validationScene({1});
        scene.batch.resource_truncated_ = true;
        scene.batch.diagnostics_.push_back("fixture input diagnostic");
        auto affine = scene.batch.hypotheses_[0].affine_transform_.clone();
        auto output = verifyResidual(scene, 1, "G05");
        check(scene.batch.hypotheses_[0].validation_residual_ == -1 &&
                  cv::norm(affine, scene.batch.hypotheses_[0].affine_transform_, cv::NORM_INF) ==
                      0 &&
                  output.resource_truncated_ && output.diagnostics_ == scene.batch.diagnostics_,
              "G05 input modified or batch metadata lost");
    }

    void residualCompletenessBranches()
    {
        auto scene = validationScene({1});
        scene.components[0].area_ = 1;
        auto area = verifyResidual(scene, 1, "G06-area");
        check(area.hypotheses_[0].completeness_ == mark::GeometryCompleteness::CLEARLY_INCOMPLETE,
              "G06 area branch changed");
        scene = validationScene({1});
        scene.components[0].touches_border_ = true;
        auto border = verifyResidual(scene, 1, "G06-border");
        check(border.hypotheses_[0].completeness_ == mark::GeometryCompleteness::CLEARLY_INCOMPLETE,
              "G06 completeness branch changed");
    }

    void residualInvalidInputs()
    {
        auto reject = [](const ValidationScene &scene)
        {
            check(mark::validateGeometryBatch(scene.batch, scene.model, scene.components,
                                              scene.config)
                      .hypotheses_.empty(),
                  "G07 unmeasurable input published as valid residual");
        };
        auto scene = validationScene({0});
        scene.components.clear();
        reject(scene);
        scene = validationScene({0});
        scene.model.polygons.clear();
        reject(scene);
        scene = validationScene({0});
        scene.batch.hypotheses_[0].assignments_.clear();
        reject(scene);
        scene = validationScene({0});
        scene.components[0].contour_.clear();
        reject(scene);
        scene = validationScene({0});
        scene.model.polygons[0].vertices.clear();
        reject(scene);
        scene = validationScene({0});
        scene.batch.hypotheses_[0].affine_transform_.at<double>(0, 0) =
            std::numeric_limits<double>::quiet_NaN();
        reject(scene);
        scene = validationScene({0});
        scene.model.polygons[0].vertices[0].x = std::numeric_limits<float>::infinity();
        reject(scene);
    }

    void residualNoncontiguousIds()
    {
        auto scene = validationScene({0, 1, 3});
        std::reverse(scene.components.begin(), scene.components.end());
        verifyResidual(scene, 3, "G08");
    }
} // namespace

int main()
{
    return temporal_fixture::run({{"actual_model_three_l", real_three_l},
                                  {"G01_actual_zero", residualZero},
                                  {"G02_one_work_pixel", residualOne},
                                  {"G03_maximum_not_global_mean", residualMaximum},
                                  {"G04_comparison_boundary", residualBoundary},
                                  {"G05_input_unchanged", residualInputUnchanged},
                                  {"G06_completeness_branches", residualCompletenessBranches},
                                  {"G07_unmeasurable_inputs", residualInvalidInputs},
                                  {"G08_noncontiguous_ids", residualNoncontiguousIds}});
}
