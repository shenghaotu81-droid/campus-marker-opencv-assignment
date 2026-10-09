// 输入当前帧、已建立对应和模型；编排四个原图角取证，四角均成功才返回测量。
// 旧最近白块fallback和低分辨率轮廓拟合已删除；模型投影只传给搜索/验证。
#include "corners/corner_resolver.hpp"
#include "corners/corner_observation.hpp"
#include "corners/corner_edge_fit.hpp"
#include "corners/sandbox_periodic.hpp"
#include "core/corner_budget.hpp"
#include "core/observed_geometry_utils.hpp"
#include <map>
#include <set>

namespace mark
{
    CornerResolution resolveObservedCorners(const PreparedFrame &frame,
                                            const GeometryHypothesis &hypothesis,
                                            const MarkerGeometry &model, const CornerConfig &config, OriginalContourIndex &index)
    {
        validateCornerObservationBudget(config);
        CornerResolution result{};
        result.status_ = CornerResolutionStatus::FAILED;
        // 格式/映射非法与图像缺边分开登记，严禁除零或访问非法affine。
        if (frame.original_image_.empty() || frame.original_image_.dims != 2 ||
            frame.original_image_.type() != CV_8UC3 || frame.image_.empty() ||
            frame.image_.dims != 2 || frame.image_.type() != CV_8UC3 ||
            !std::isfinite(frame.scale_x_) || !std::isfinite(frame.scale_y_) ||
            frame.scale_x_ <= 0 || frame.scale_y_ <= 0 || frame.original_white_threshold_ < 0 ||
            frame.original_white_threshold_ > 255 ||
            !observed::validAffine(hypothesis.affine_transform_))
        {
            result.rejection_reason_ = "INVALID_CORNER_INPUT: 原图/映射/非镜像仿射非法";
            return result;
        }
        // 显式映射必须和真实resize尺寸吻合，不能让任意正scale把搜索域错映后“成功”。
        if (frame.scale_x_ != static_cast<double>(frame.image_.cols) / frame.original_image_.cols ||
            frame.scale_y_ != static_cast<double>(frame.image_.rows) / frame.original_image_.rows)
        {
            result.rejection_reason_ = "INVALID_COORDINATE_MAPPING";
            return result;
        }
        if (hypothesis.completeness_ == GeometryCompleteness::CLEARLY_INCOMPLETE)
        {
            result.rejection_reason_ = "CLEARLY_INCOMPLETE: 父假设已明确不完整";
            return result;
        }
        std::map<size_t, const WhiteComponent *> components;
        for (const auto &c : frame.components_)
            if (!components.emplace(c.component_id_, &c).second)
            {
                result.rejection_reason_ = "DUPLICATE_COMPONENT_ID";
                return result;
            }
        std::map<std::string, size_t> assignments;
        std::set<size_t> used;
        for (const auto &a : hypothesis.assignments_)
        {
            if (!assignments.emplace(a.model_part_id_, a.component_id_).second ||
                !used.insert(a.component_id_).second || !components.count(a.component_id_))
            {
                result.rejection_reason_ = "INVALID_ASSIGNMENT: 重复身份/一片多身份/悬空引用";
                return result;
            }
        }

        struct Binding
        {
            const char *part;
            int vertex, a, b;
        };

        // 图纸绑定已核对；边编号ei=vi到v(i+1)，物理P1始终是M外凸角。
        const Binding bindings[] = {
            {"L0", 0, 5, 0}, {"M1", 1, 0, 1}, {"L2", 0, 5, 0}, {"L3", 0, 5, 0}};
        CornerMeasurement measurement{};
        for (int i = 0; i < 4; ++i)
        {
            const auto &binding = bindings[i];
            auto assignment = assignments.find(binding.part);
            if (assignment == assignments.end())
            {
                result.rejection_reason_ = i == 1 ? "MISSING_M_ASSIGNMENT" : "MISSING_L_ASSIGNMENT";
                return result;
            }
            const GeometryPolygon *polygon = nullptr;
            for (const auto &p : model.polygons)
                if (p.id == binding.part)
                    polygon = &p;
            if (!polygon ||
                polygon->vertices.size() <=
                    static_cast<size_t>(std::max({binding.vertex, binding.a, binding.b})))
            {
                result.rejection_reason_ = "INVALID_CORNER_BINDING";
                return result;
            }
            auto contour = observeOriginalContour(frame, *components.at(assignment->second),
                                                  *config.observation_budget_, index);
            if (!contour.reason.empty())
            {
                result.rejection_reason_ = contour.reason + ": P" + std::to_string(i);
                return result;
            }
            std::array<std::array<cv::Point2d, 2>, 2> edges;
            int edge_ids[] = {binding.a, binding.b};
            for (int e = 0; e < 2; ++e)
                for (int end = 0; end < 2; ++end)
                {
                    size_t vertex = (edge_ids[e] + end) % polygon->vertices.size();
                    edges[e][end] = frame.workToOriginal(
                        observed::project(hypothesis.affine_transform_, polygon->vertices[vertex]));
                    if (!observed::finite(edges[e][end]))
                    {
                        result.rejection_reason_ = "INVALID_MODEL_PROJECTION";
                        return result;
                    }
                }
            sandbox::frame_shift=index.parity_shift;
            auto fit = fitObservedEdgePair(contour.contour, edges, config);
            if (!fit.evidence)
            {
                result.rejection_reason_ = fit.reason + ": P" + std::to_string(i);
                return result;
            }
            auto &evidence = *fit.evidence;
            evidence.physical_corner_ = static_cast<PhysicalCorner>(i);
            evidence.frame_id_ = frame.frame_id_;
            evidence.component_id_ = assignment->second;
            evidence.model_edge_ids_ = {binding.a, binding.b};
            evidence.model_vertex_id_ = binding.vertex;
            evidence.stable_id_ = std::to_string(frame.frame_id_) + "/" +
                                  std::to_string(assignment->second) + "/" +
                                  std::to_string(binding.a) + ":" +
                                  std::to_string(evidence.observed_segment_ids_[0]) + ":" +
                                  std::to_string(evidence.observed_segment_end_ids_[0]) + "/" +
                                  std::to_string(binding.b) + ":" +
                                  std::to_string(evidence.observed_segment_ids_[1]) + ":" +
                                  std::to_string(evidence.observed_segment_end_ids_[1]);
            measurement.physical_corners_[i] = evidence.intersection_;
            measurement.evidence_[i] = std::move(evidence);
        }
        if (orderScreenCorners(measurement.physical_corners_, config).status_ !=
            ScreenOrderStatus::SUCCESS)
        {
            result.rejection_reason_ = "INVALID_MEASURED_QUADRILATERAL";
            return result;
        }
        result.status_ = CornerResolutionStatus::SUCCESS;
        result.measurement_ = std::move(measurement);
        return result;
    }
    // 内部旧入口仍可用；正式decode把同帧索引在全部父假设间共享。
    CornerResolution resolveObservedCorners(const PreparedFrame&frame,const GeometryHypothesis&hypothesis,
      const MarkerGeometry&model,const CornerConfig&config){OriginalContourIndex index;return resolveObservedCorners(frame,hypothesis,model,config,index);}

}
