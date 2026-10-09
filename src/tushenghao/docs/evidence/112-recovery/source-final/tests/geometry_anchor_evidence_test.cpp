// Path A 主动检查；内部手造六边候选检验枚举，真实轮廓另由 A09 覆盖。
#include "geometry/geometry_anchor_evidence.hpp"
#include "geometry/geometry_l_topology.hpp"
#include "geometry/geometry_matcher.hpp"
#include "geometry/geometry_observation.hpp"
#include "core/observed_geometry_utils.hpp"
#include "block3_fixture.hpp"
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <sstream>
#include <iomanip>
using namespace mark;

namespace
{
    void check(bool b, const char *s)
    {
        if (!b)
            throw std::runtime_error(s);
    }

    LTopologyCandidate topology(const GeometryPolygon &p, cv::Point2f anchor)
    {
        LTopologyCandidate t;
        t.simplification_epsilon_ = 1;
        for (auto v : p.vertices)
            t.polygon_.push_back((v - p.anchor) * 2 + anchor);
        for (size_t i = 0; i < p.vertices.size(); ++i)
            if (p.vertices[i] == p.anchor)
                t.concave_vertex_indices_.push_back(i);
        check(t.concave_vertex_indices_.size() == 1, "fixture anchor not vertex");
        return t;
    }

    std::vector<ShapeObservation> observations(bool two = true)
    {
        std::vector<ShapeObservation> os;
        size_t n = 0;
        for (auto &p : fixture::model().polygons)
            if (p.id[0] == 'L')
            {
                ShapeObservation o;
                o.source_component_id_ = 101 + n * 37;
                o.supported_classes_ = {"L"};
                auto good = p.anchor * 2 + cv::Point2f(100, 150),
                     bad = good + cv::Point2f(3 + 2 * n, 2 - int(n));
                o.simplified_polygon_ = {bad, good};
                o.turns_ = {{0, TurnType::CONCAVE}, {1, TurnType::CONCAVE}};
                o.anchor_vertex_index_ = 0;
                if (two)
                    o.l_topology_candidates_.push_back(topology(p, bad));
                o.l_topology_candidates_.push_back(topology(p, good));
                os.push_back(o);
                ++n;
            }
        return os;
    }

    const GeometryPolygon &first(const MarkerGeometry &m)
    {
        for (auto &p : m.polygons)
            if (p.id[0] == 'L')
                return p;
        throw std::runtime_error("missing L");
    }

    void a01()
    {
        auto m = fixture::model();
        auto t = topology(first(m), {20, 30});
        check(is_valid_l_topology_candidate(t), "valid six-edge L rejected");
        ShapeObservation o;
        o.l_topology_candidates_ = {t};
        auto c = collect_observed_l_anchors(o, 57);
        check(c.anchors_.size() == 1 && c.anchors_[0].point_ == cv::Point2f(20, 30) &&
                  c.anchors_[0].source_component_id_ == 57,
              "A01 source/point");
    }

    void a02()
    {
        auto m = fixture::model();
        auto good = topology(first(m), {20, 30});
        std::vector<LTopologyCandidate> bad;
        for (size_t n : {size_t(1), size_t(2), size_t(4)})
        {
            auto t = good;
            t.polygon_.resize(n);
            bad.push_back(t);
        }
        for (auto &p : m.polygons)
            if (p.id[0] == 'M')
            {
                LTopologyCandidate t;
                t.simplification_epsilon_ = 1;
                for (auto v : p.vertices)
                    t.polygon_.push_back(v);
                for (size_t i = 0; i < t.polygon_.size(); ++i)
                    if (p.vertices[i] == p.anchor)
                        t.concave_vertex_indices_.push_back(i);
                bad.push_back(t);
            }
        for (float x :
             {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
        {
            auto t = good;
            t.polygon_[0].x = x;
            bad.push_back(t);
        }
        for (double e : {0., -1., std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN()})
        {
            auto t = good;
            t.simplification_epsilon_ = e;
            bad.push_back(t);
        }
        for (auto ids : std::vector<std::vector<size_t>>{
                 {}, {6}, {(good.concave_vertex_indices_[0] + 1) % 6}, {0, 1}})
        {
            auto t = good;
            t.concave_vertex_indices_ = ids;
            bad.push_back(t);
        }
        ShapeObservation o;
        o.l_topology_candidates_ = bad;
        for (auto &t : bad)
            check(!is_valid_l_topology_candidate(t), "invalid topology admitted");
        auto c = collect_observed_l_anchors(o, 71);
        check(c.anchors_.empty() && c.rejected_topology_candidates_ == bad.size(),
              "invalid count/fallback");
    }

    void a03()
    {
        for (auto &o : observations(false))
        {
            auto c = collect_observed_l_anchors(o, o.source_component_id_);
            check(c.anchors_.size() == 1 && c.anchors_[0].point_ == o.simplified_polygon_[1],
                  "raw first concavity borrowed L identity");
        }
    }

    void a04()
    {
        auto os = observations();
        for (auto &o : os)
        {
            o.l_topology_candidates_.clear();
            check(collect_observed_l_anchors(o, o.source_component_id_).anchors_.empty(),
                  "fallback retained");
        }
        check(generateGeometryHypotheses(os, fixture::model(), {}).hypotheses_.empty(),
              "unsupported parent generated");
    }

    void a05()
    {
        auto os = observations();
        auto m = fixture::model();
        for (auto &o : os)
            check(collect_observed_l_anchors(o, o.source_component_id_).anchors_.size() == 2,
                  "second anchor lost");
        bool found = false;
        for (auto &h : generateGeometryHypotheses(os, m, {}).hypotheses_)
        {
            bool good = true;
            for (auto &a : h.assignments_)
            {
                auto o = std::find_if(os.begin(), os.end(),
                                      [&](auto &o)
                                      {
                                          return o.source_component_id_ == a.component_id_;
                                      });
                auto p = std::find_if(m.polygons.begin(), m.polygons.end(),
                                      [&](auto &p)
                                      {
                                          return p.id == a.model_part_id_;
                                      });
                good = good && cv::norm(observed::project(h.affine_transform_, p->anchor) -
                                        cv::Point2d(o->simplified_polygon_[1])) < 1e-5;
            }
            found |= good;
        }
        check(found, "second tuple omitted");
    }

    void a06()
    {
        auto o = observations(false)[0];
        auto t = o.l_topology_candidates_[0];
        o.l_topology_candidates_.push_back(t);
        auto c = collect_observed_l_anchors(o, 909);
        check(c.anchors_.size() == 1 && c.anchors_[0].topology_supports_.size() == 2,
              "same point sources lost");
        auto &s = c.anchors_[0].topology_supports_;
        check(s[0].topology_candidate_index_ == 0 && s[1].topology_candidate_index_ == 1 &&
                  s[0].concave_vertex_index_ == s[1].concave_vertex_index_,
              "source order changed");
    }

    // 组合内 assignment 随观察容器顺序变化，比较映射／实测锚点，而非向量排列。
    std::set<std::string> signatures(const GeometryBatch &b)
    {
        std::set<std::string> out;
        for (auto &h : b.hypotheses_)
        {
            std::vector<std::string> entries;
            for (auto &proof : h.evidence_)
                if (proof.rfind("anchor_topology/v1 ", 0) == 0)
                    entries.push_back(proof);
            std::sort(entries.begin(), entries.end());
            std::ostringstream s;
            for (auto &e : entries)
                s << e << ';';
            out.insert(s.str());
        }
        return out;
    }

    void a07()
    {
        auto os = observations(), copy = os;
        std::reverse(copy.begin(), copy.end());
        auto m = fixture::model();
        auto a = generateGeometryHypotheses(os, m, {}), b = generateGeometryHypotheses(copy, m, {});
        check(!a.hypotheses_.empty() && signatures(a) == signatures(b),
              "source IDs/order changed parent set");
        for (auto &h : a.hypotheses_)
            for (auto &x : h.assignments_)
            {
                auto o = std::find_if(os.begin(), os.end(),
                                      [&](auto &o)
                                      {
                                          return x.component_id_ == o.source_component_id_;
                                      });
                check(o != os.end(), "ID replaced with index");
                auto p = std::find_if(m.polygons.begin(), m.polygons.end(),
                                      [&](auto &p)
                                      {
                                          return p.id == x.model_part_id_;
                                      });
                auto projected = observed::project(h.affine_transform_, p->anchor);
                auto anchors = collect_observed_l_anchors(*o, x.component_id_);
                check(std::any_of(anchors.anchors_.begin(), anchors.anchors_.end(),
                                  [&](auto &anchor)
                                  {
                                      return cv::norm(projected - cv::Point2d(anchor.point_)) <
                                             1e-5;
                                  }),
                      "parent affine no longer fits measured source");
            }
    }

    void a08()
    {
        auto os = observations();
        GeometryConfig cfg;
        cfg.max_hypothesis_count_ = 48;
        auto a = generateGeometryHypotheses(os, fixture::model(), cfg);
        check(!a.resource_truncated_, "exact K truncated");
        check(std::find(a.diagnostics_.begin(), a.diagnostics_.end(),
                        "geometry/anchor_expansions=48") != a.diagnostics_.end(),
              "invalid affines escaped budget");
        check(a.hypotheses_.size() < 48, "fixture no invalid affine");
        cfg.max_hypothesis_count_ = 47;
        auto b = generateGeometryHypotheses(os, fixture::model(), cfg);
        check(b.resource_truncated_, "pending tuple not truncated");
    }

    void a09()
    {
        for (auto name :
             {"l-topology-rounded-L3.txt", "l-topology-video2-L.txt", "l-topology-short-M1.txt"})
        {
            std::ifstream f(std::filesystem::path(__FILE__).parent_path() / "data" / name);
            size_t n = 0;
            f >> n;
            WhiteComponent c;
            c.component_id_ = 137;
            for (size_t i = 0; i < n; ++i)
            {
                int x, y;
                f >> x >> y;
                c.contour_.push_back({x, y});
            }
            check(bool(f), "real contour missing");
            c.area_ = std::abs(cv::contourArea(c.contour_));
            c.bounding_box_ = cv::boundingRect(c.contour_);
            auto o = observeShapes({c}, {}).at(0);
            auto collected = collect_observed_l_anchors(o, c.component_id_);
            if (std::string(name).find("short-M") != std::string::npos)
                check(collected.anchors_.empty(), "M became L");
            else
            {
                check(!collected.anchors_.empty(), "real L excluded");
                for (auto &a : collected.anchors_)
                    for (auto &s : a.topology_supports_)
                    {
                        auto &t = o.l_topology_candidates_.at(s.topology_candidate_index_);
                        check(is_valid_l_topology_candidate(t) &&
                                  t.polygon_.at(s.concave_vertex_index_) == a.point_,
                              "real source invalid");
                    }
            }
        }
    }
}

int main(int argc, char **argv)
{
    bool helper = argc == 2 && std::string(argv[1]) == "--helper-only";
    int failed = 0;
    for (auto test : std::vector<std::pair<const char *, std::function<void()>>>{{"A01", a01},
                                                                                 {"A02", a02},
                                                                                 {"A03", a03},
                                                                                 {"A04", a04},
                                                                                 {"A05", a05},
                                                                                 {"A06", a06},
                                                                                 {"A07", a07},
                                                                                 {"A08", a08},
                                                                                 {"A09", a09}})
    {
        if (helper && std::set<std::string>{"A04", "A05", "A07", "A08"}.count(test.first))
            continue;
        try
        {
            test.second();
            std::cout << "PASS " << test.first << '\n';
        }
        catch (std::exception &e)
        {
            ++failed;
            std::cerr << "FAIL " << test.first << ": " << e.what() << '\n';
        }
    }
    return failed ? 1 : 0;
}
