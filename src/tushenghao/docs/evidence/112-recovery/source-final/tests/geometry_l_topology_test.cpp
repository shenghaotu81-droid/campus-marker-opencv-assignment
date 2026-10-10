// 真实圆角L/M反例与多锚点资源边界；主动检查在Release同样生效。
#include "geometry/geometry_observation.hpp"
#include "geometry/geometry_matcher.hpp"
#include "geometry/geometry_validation.hpp"
#include "core/observed_geometry_utils.hpp"
#include "block3_fixture.hpp"
#include <fstream>
#include <functional>
#include <iostream>
#include <algorithm>
#include <stdexcept>

namespace
{
    void require(bool good, const char *reason)
    {
        if (!good)
            throw std::runtime_error(reason);
    }

    // 数据来自H已确认的实例身份；测试侧读取固定像素，正式观察入口不读真值。
    mark::WhiteComponent contour(const char *name)
    {
        std::ifstream in(std::filesystem::path(__FILE__).parent_path() / "data" / name);
        size_t n;
        in >> n;
        mark::WhiteComponent c;
        c.component_id_ = 0;
        for (size_t i = 0; i < n; ++i)
        {
            int x, y;
            in >> x >> y;
            c.contour_.push_back({x, y});
        }
        require(bool(in), "fixture missing");
        c.area_ = std::abs(cv::contourArea(c.contour_));
        c.bounding_box_ = cv::boundingRect(c.contour_);
        return c;
    }

    bool hasL(const mark::ShapeObservation &o)
    {
        return std::find(o.supported_classes_.begin(), o.supported_classes_.end(), "L") !=
               o.supported_classes_.end();
    }

    void roundedL()
    {
        auto c = contour("l-topology-rounded-L3.txt");
        auto o = mark::observeShapes({c}, mark::GeometryConfig{});
        require(hasL(o.at(0)), "actual L excluded by vertex-count class");
    }

    void shortM()
    {
        auto c = contour("l-topology-short-M1.txt");
        auto o = mark::observeShapes({c}, mark::GeometryConfig{});
        require(!hasL(o.at(0)), "short M impersonates L");
    }

    // 视频帧2的完整实测轮廓：观察侧不能以epsilon闭合门槛提前剔除父验证可接纳的L。
    void noisyL()
    {
        auto c = contour("l-topology-video2-L.txt");
        require(hasL(mark::observeShapes({c}, mark::GeometryConfig{}).at(0)),
                "epsilon closure discards observed long-arm L");
    }

    // 内部枚举 fixture：两个完整长臂 L 分别支持不同实测点，并非同一真实轮廓证明。
    std::vector<mark::ShapeObservation> anchors(const mark::MarkerGeometry &model)
    {
        std::vector<mark::ShapeObservation> os;
        int i = 0;
        for (auto &p : model.polygons)
            if (p.id[0] == 'L')
            {
                mark::ShapeObservation o;
                auto good = p.anchor * 2 + cv::Point2f(100, 150);
                auto bad = good + cv::Point2f(float(3 + i * 2), float(2 - i));
                o.simplified_polygon_ = {bad, good};
                o.turns_ = {{0, mark::TurnType::CONCAVE}, {1, mark::TurnType::CONCAVE}};
                o.anchor_vertex_index_ = 0;
                o.supported_classes_ = {"L"};
                for (auto anchor : {bad, good})
                {
                    mark::LTopologyCandidate t;
                    t.simplification_epsilon_ = 1;
                    for (size_t j = 0; j < p.vertices.size(); ++j)
                    {
                        t.polygon_.push_back((p.vertices[j] - p.anchor) * 2 + anchor);
                        if (p.vertices[j] == p.anchor)
                            t.concave_vertex_indices_.push_back(j);
                    }
                    o.l_topology_candidates_.push_back(t);
                }
                os.push_back(o);
                ++i;
            }
        return os;
    }

    void allAnchors()
    {
        auto model = fixture::model();
        auto os = anchors(model);
        mark::GeometryConfig cfg;
        auto batch = mark::generateGeometryHypotheses(os, model, cfg);
        bool found = false;
        for (auto &h : batch.hypotheses_)
        {
            bool equal = true;
            for (auto &a : h.assignments_)
            {
                auto p = std::find_if(model.polygons.begin(), model.polygons.end(),
                                      [&](auto &p)
                                      {
                                          return p.id == a.model_part_id_;
                                      });
                auto predicted = mark::observed::project(h.affine_transform_, p->anchor);
                equal = equal &&
                        cv::norm(predicted -
                                 cv::Point2d(os[a.component_id_].simplified_polygon_[1])) < 1e-5;
            }
            found = found || equal;
        }
        require(found, "all observed concave anchors not enumerated");
    }

    // 裸凹点不能借 L 类别获得资格；保留合法 good，禁止 unsupported bad 参与任何拟合。
    void originalAnchors()
    {
        auto model = fixture::model();
        auto os = anchors(model);
        for (auto &o : os)
            o.l_topology_candidates_.erase(o.l_topology_candidates_.begin());
        auto batch = mark::generateGeometryHypotheses(os, model, mark::GeometryConfig{});
        bool found = false;
        for (auto &h : batch.hypotheses_)
        {
            bool good = true;
            for (auto &a : h.assignments_)
            {
                auto p = std::find_if(model.polygons.begin(), model.polygons.end(),
                                      [&](auto &p)
                                      {
                                          return p.id == a.model_part_id_;
                                      });
                auto predicted = mark::observed::project(h.affine_transform_, p->anchor);
                require(cv::norm(predicted -
                                 cv::Point2d(os[a.component_id_].simplified_polygon_[0])) > 1e-5,
                        "unsupported raw anchor admitted");
                good = good &&
                       cv::norm(predicted -
                                cv::Point2d(os[a.component_id_].simplified_polygon_[1])) < 1e-5;
            }
            found = found || good;
        }
        require(found, "legal anchor omitted");
    }

    // 搜索上限计每个锚点元组，包含无效仿射；精确48穷尽与第49之前截断分别检查。
    void anchorResource()
    {
        auto model = fixture::model();
        auto os = anchors(model);
        mark::GeometryConfig cfg;
        cfg.max_hypothesis_count_ = 48;
        auto exact = mark::generateGeometryHypotheses(os, model, cfg);
        require(!exact.resource_truncated_, "exact K falsely truncated");
        require(std::find(exact.diagnostics_.begin(), exact.diagnostics_.end(),
                          "geometry/anchor_expansions=48") != exact.diagnostics_.end(),
                "anchor budget denominator wrong");
        cfg.max_hypothesis_count_ = 47;
        auto tail = mark::generateGeometryHypotheses(os, model, cfg);
        require(tail.resource_truncated_, "pending anchor tuple not truncated");
        auto propagated = mark::validateGeometryBatch(tail, model, {}, cfg);
        require(propagated.resource_truncated_, "validator lost truncation");
        require(std::find(propagated.diagnostics_.begin(), propagated.diagnostics_.end(),
                          "geometry/anchor_expansions=47") != propagated.diagnostics_.end(),
                "validator lost anchor trace");
    }

    // 来源ID与观察容器顺序独立，新观察和旧验证不得用ID作数组下标。
    void stableIds()
    {
        auto scene = fixture::scene();
        mark::GeometryConfig cfg;
        auto os = mark::observeShapes(scene.frame.components_, cfg);
        auto batch =
            mark::validateGeometryBatch(mark::generateGeometryHypotheses(os, scene.geometry, cfg),
                                        scene.geometry, scene.frame.components_, cfg);
        require(!batch.hypotheses_.empty(), "non-contiguous source IDs lost");
        for (auto &h : batch.hypotheses_)
            for (auto &a : h.assignments_)
                require(std::any_of(scene.frame.components_.begin(), scene.frame.components_.end(),
                                    [&](auto &c)
                                    {
                                        return c.component_id_ == a.component_id_;
                                    }),
                        "source ID invented");
    }

    // 轮廓绕向不应改变L/M拓扑；矩形没有真实凹点，不因多候选被提升为L。
    void windingAndRectangle()
    {
        auto c = contour("l-topology-rounded-L3.txt");
        std::reverse(c.contour_.begin(), c.contour_.end());
        require(hasL(mark::observeShapes({c}, mark::GeometryConfig{}).at(0)),
                "winding changes L identity");
        c.contour_ = {{0, 0}, {20, 0}, {20, 20}, {0, 20}};
        require(!hasL(mark::observeShapes({c}, mark::GeometryConfig{}).at(0)),
                "rectangle becomes L");
    }

    // 白阈值严格大于200，201只是必要像素条件，不保证L拓扑或Detection。
    void brightnessBoundary()
    {
        mark::FrameInput input{};
        mark::PreprocessConfig pre;
        pre.work_width = 40;
        pre.work_height = 40;
        pre.threshold = 200;
        mark::GeometryConfig cfg;
        input.image = cv::Mat(40, 40, CV_8UC3, cv::Scalar(200, 200, 200));
        auto frame = mark::preprocess(input, pre);
        require(mark::extractWhiteComponents(frame, cfg).empty(), "gray200 counted as white");
        input.image = cv::Mat(40, 40, CV_8UC3, cv::Scalar(201, 201, 201));
        frame = mark::preprocess(input, pre);
        auto cs = mark::extractWhiteComponents(frame, cfg);
        require(cs.size() == 1, "gray201 not counted as white");
        require(!hasL(mark::observeShapes(cs, cfg).at(0)), "bright rectangle promises L");
    }
}

int main()
{
    int failures = 0;
    for (auto t : std::vector<std::pair<const char *, std::function<void()>>>{
             {"RoundedL", roundedL},
             {"ShortM", shortM},
             {"NoisyL", noisyL},
             {"AllConcaveAnchors", allAnchors},
             {"UnsupportedRawConcaveAnchors", originalAnchors},
             {"AnchorResource", anchorResource},
             {"StableIds", stableIds},
             {"WindingAndRectangle", windingAndRectangle},
             {"BrightnessBoundary", brightnessBoundary}})
    {
        try
        {
            t.second();
            std::cout << "PASS " << t.first << '\n';
        }
        catch (const std::exception &e)
        {
            ++failures;
            std::cerr << "FAIL " << t.first << ": " << e.what() << '\n';
        }
    }
    return failures ? 1 : 0;
}
