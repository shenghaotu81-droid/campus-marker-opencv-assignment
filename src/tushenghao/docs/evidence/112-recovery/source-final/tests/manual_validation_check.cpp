// TODO: 赛前用合成器做完整验证后删除
/* 本次简单验证逻辑：
1. 用完美 mock 数据（3 个标准 L，位置都对）跑 Step 6，得到 6 个假设
2. 喂给 Step 7 验证
3. 算对的标准：validated 非空（至少留下一个）
这个测试只验"不误杀"，没验"能抓错"。要验抓错，得喂错数据（比如把 affine 搞歪）
下一步：
近的（可选）：造一组错数据（比如把某个 observation 的坐标故意挪歪 20 像素），看 Step 7 能不能把它拦下来。这是验"抓错"能力。
远的（必须）：Step 8，把 5→6→7 串进 detector.cpp，让它真正跑起来。（detector 集成：不是做新算法，而是把已经冻结的 Block 2 模块接入 Detector 主流程。）
现在不做，Step 9 全回归时一起补。
*/
// mock 测试:直接造假数据调 validateGeometryBatch，只验逻辑对不对，不用等合成器。
// 赛前调参（Step 4 的完整测量）:合成器做精细调参,生成带真值的图像，跑完整管线（适合调参数）。
#include "geometry/geometry_matcher.hpp"
#include "geometry/geometry_l_topology.hpp"
#include "geometry/geometry_validation.hpp"
#include "core/marker_geometry.hpp"
#include "mark/detector_config.hpp"

#include <iostream>

namespace
{

    // debug
    // mark::ShapeObservation createLObservation(std::size_t id_offset)
    // {
    //     mark::ShapeObservation observation;
    //     observation.simplified_polygon_ = {
    //         {0.0f + static_cast<float>(id_offset), 0.0f},
    //         {32.0f + static_cast<float>(id_offset), 0.0f},
    //         {32.0f + static_cast<float>(id_offset), 8.0f},
    //         {8.0f + static_cast<float>(id_offset), 8.0f},
    //         {8.0f + static_cast<float>(id_offset), 32.0f},
    //         {0.0f + static_cast<float>(id_offset), 32.0f}};
    //     mark::TurnFeature turn;
    //     turn.vertex_index_ = 3;
    //     turn.type_ = mark::TurnType::CONCAVE;
    //     observation.turns_.push_back(turn);
    //     observation.supported_classes_.push_back("L");
    //     observation.anchor_vertex_index_ = 3;
    //     return observation;
    // }

    mark::ShapeObservation createLObservation(float cx, float cy)
    {
        mark::ShapeObservation observation;
        // L 形 6 顶点，以 (cx,cy) 为锚点（内凹角）为基准
        // 顶点相对位置：锚点是 vertices[3] = (8,8) 相对 (0,0) 的偏移
        float ox = cx - 8.0f;
        float oy = cy - 8.0f;
        observation.simplified_polygon_ = {{ox + 0.0f, oy + 0.0f},  {ox + 32.0f, oy + 0.0f},
                                           {ox + 32.0f, oy + 8.0f}, {ox + 8.0f, oy + 8.0f},
                                           {ox + 8.0f, oy + 32.0f}, {ox + 0.0f, oy + 32.0f}};
        mark::TurnFeature turn;
        turn.vertex_index_ = 3;
        turn.type_ = mark::TurnType::CONCAVE;
        observation.turns_.push_back(turn);
        observation.supported_classes_.push_back("L");
        observation.anchor_vertex_index_ = 3;
        // Path A：mock 也要通过原观察入口取得完整长臂 L 来源，不能依靠类别/裸凹点回填。
        mark::WhiteComponent component;
        for (auto p : observation.simplified_polygon_)
            component.contour_.emplace_back(p);
        observation.l_topology_candidates_ =
            mark::observeLTopologies(component, mark::GeometryConfig{});
        return observation;
    }

    //debug
    // mark::MarkerGeometry createSimpleGeometry()
    // {
    //     mark::MarkerGeometry geometry;
    //     geometry.schema_version = 1;
    //     for (const auto &id : {"L0", "L1", "L2"})
    //     {
    //         mark::GeometryPolygon polygon;
    //         polygon.id = id;
    //         polygon.vertices = {
    //             {0.0f, 0.0f}, {32.0f, 0.0f}, {32.0f, 8.0f}, {8.0f, 8.0f}, {8.0f, 32.0f}, {0.0f, 32.0f}};
    //         polygon.anchor = polygon.vertices[3];
    //         polygon.area = 448.0;
    //         geometry.polygons.push_back(polygon);
    //     }
    //     return geometry;
    // }

    // mark::MarkerGeometry createSimpleGeometry()
    // {
    //     mark::MarkerGeometry geometry;
    //     geometry.schema_version = 1;
    //     // 2026-10-05 修正：3 个 L 的 anchor 必须不同，
    //     // 否则 estimateAffine2D 输入 3 个重合源点，算出退化仿射。
    //     // 用真实模型同款坐标：L0(8,8), L1(72,72), L2(8,72)
    //     const std::vector<cv::Point2f> anchors = {
    //         {8.0f, 8.0f}, {72.0f, 72.0f}, {8.0f, 72.0f}};
    //     int idx = 0;
    //     for (const auto &id : {"L0", "L1", "L2"})
    //     {
    //         mark::GeometryPolygon polygon;
    //         polygon.id = id;
    //         polygon.vertices = {
    //             {0.0f, 0.0f}, {32.0f, 0.0f}, {32.0f, 8.0f}, {8.0f, 8.0f}, {8.0f, 32.0f}, {0.0f, 32.0f}};
    //         polygon.anchor = anchors[idx++];
    //         polygon.area = 448.0;
    //         geometry.polygons.push_back(polygon);
    //     }
    //     return geometry;
    // }

    mark::MarkerGeometry createSimpleGeometry()
    {
        mark::MarkerGeometry geometry;
        geometry.schema_version = 1;
        // 2026-10-05 修正：顶点必须与 anchor 一致，
        // 否则投影后的顶点与观测轮廓差几十像素，全被误拒。
        const std::vector<cv::Point2f> anchors = {{8.0f, 8.0f}, {72.0f, 72.0f}, {8.0f, 72.0f}};
        int idx = 0;
        for (const auto &id : {"L0", "L1", "L2"})
        {
            mark::GeometryPolygon polygon;
            polygon.id = id;
            cv::Point2f a = anchors[idx++];
            // 顶点以 anchor 为基准：anchor 是 vertices[3] = (8,8) 相对 (0,0)
            float ox = a.x - 8.0f;
            float oy = a.y - 8.0f;
            polygon.vertices = {{ox + 0.0f, oy + 0.0f},  {ox + 32.0f, oy + 0.0f},
                                {ox + 32.0f, oy + 8.0f}, {ox + 8.0f, oy + 8.0f},
                                {ox + 8.0f, oy + 32.0f}, {ox + 0.0f, oy + 32.0f}};
            polygon.anchor = a;
            polygon.area = 448.0;
            geometry.polygons.push_back(polygon);
        }
        return geometry;
    }

    // mark::WhiteComponent createWhiteComponent(std::size_t id_offset)
    // {
    //     mark::WhiteComponent comp;
    //     comp.area_ = 448.0;
    //     int o = static_cast<int>(id_offset);
    //     comp.contour_ = {{o, 0}, {o + 20, 0}, {o + 20, 10}, {o + 10, 10}, {o + 10, 20}, {o, 20}};
    //     comp.touches_border_ = false;
    //     return comp;
    // }
    mark::WhiteComponent createWhiteComponent(float cx, float cy)
    {
        mark::WhiteComponent comp;
        comp.area_ = 448.0;
        float ox = cx - 8.0f;
        float oy = cy - 8.0f;
        int oxi = static_cast<int>(ox);
        int oyi = static_cast<int>(oy);
        comp.contour_ = {{oxi, oyi},         {oxi + 32, oyi},     {oxi + 32, oyi + 8},
                         {oxi + 8, oyi + 8}, {oxi + 8, oyi + 32}, {oxi, oyi + 32}};
        comp.touches_border_ = false;
        return comp;
    }

} // namespace

int main()
{
    mark::GeometryConfig config;
    config.max_hypothesis_count_ = 100;
    // 2026-10-05：mock 数据是手工拼的，有 2-3 像素误差，
    // 阈值设大一点，只验"不误杀"，不验精度。
    config.max_validation_residual_ = 10.0;

    std::vector<mark::ShapeObservation> observations;
    // observations.push_back(createLObservation(0));
    // observations.push_back(createLObservation(30));
    // observations.push_back(createLObservation(60));
    // 2026-10-05 修正：观测 anchor 必须与模型 anchor 一致（恒等变换），
    // 否则 estimateAffine2D 输入共线点，算出退化仿射。
    observations.push_back(createLObservation(8.0f, 8.0f));
    observations.push_back(createLObservation(72.0f, 72.0f));
    observations.push_back(createLObservation(8.0f, 72.0f));

    std::vector<mark::WhiteComponent> components;
    // components.push_back(createWhiteComponent(0));
    // components.push_back(createWhiteComponent(30));
    // components.push_back(createWhiteComponent(60));
    components.push_back(createWhiteComponent(8.0f, 8.0f));
    components.push_back(createWhiteComponent(72.0f, 72.0f));
    components.push_back(createWhiteComponent(8.0f, 72.0f));
    // 手工组件也必须具有唯一来源ID，与对应观察的兼容索引一致。
    for (std::size_t i = 0; i < components.size(); ++i)
        components[i].component_id_ = i;

    mark::MarkerGeometry geometry = createSimpleGeometry();

    auto batch = mark::generateGeometryHypotheses(observations, geometry, config);
    std::cout << "Step6 hypotheses: " << batch.hypotheses_.size() << std::endl;

    auto validated = mark::validateGeometryBatch(batch, geometry, components, config);
    std::cout << "Step7 validated: " << validated.hypotheses_.size() << std::endl;

    for (const auto &d : validated.diagnostics_)
    {
        std::cout << "  diag: " << d << std::endl;
    }

    if (validated.hypotheses_.empty())
    {
        std::cout << "FAIL: all rejected" << std::endl;
        return 1;
    }
    std::cout << "PASS" << std::endl;
    return 0;
}
