/* Block2 Step 5 的实现:
1.extractWhiteComponents：拿图像，找白块。转灰度→二值化→找轮廓→过滤→每个轮廓包成 WhiteComponent（带轮廓、面积、外接框、碰边标记）。
2.observeShapes：拿白块，简化形状。多边形近似→存 simplified_polygon_→算每个顶点的凸凹（turns_）→给个初步的类别猜测。
*/
#include "geometry/geometry_observation.hpp"
#include "geometry/geometry_l_topology.hpp"
#include "core/prepared_frame.hpp"
#include "mark/detector_config.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>

#include <opencv2/imgproc.hpp>

namespace mark
{

    // 判断两个点形成的转折方向。
    // 图像坐标 y 向下，因此这里通过叉积符号记录几何方向。
    // 后续若坐标系统一调整，只需修改此处。(坐标系符号问题隔离)
    static TurnType classifyTurn(
        const cv::Point2f &previous,
        const cv::Point2f &current,
        const cv::Point2f &next)
    {
        const cv::Point2f v1 = current - previous;
        const cv::Point2f v2 = next - current;

        const double cross =
            v1.x * v2.y - v1.y * v2.x;

        if (cross < 0)
        {
            return TurnType::CONVEX;
        }

        return TurnType::CONCAVE;
    }

    // 提取图像中的白色连通区域。
    // 该函数只负责从像素产生 WhiteComponent，不进行 MARK 几何解释。
    std::vector<WhiteComponent> extractWhiteComponents(
        const PreparedFrame &frame,
        const GeometryConfig &config)
    {
        std::vector<WhiteComponent> components;

        // BGR 图像不能直接 threshold。
        // 先转换灰度，使后续二值分割与输入格式解耦。
        cv::Mat gray_image;

        cv::cvtColor(
            frame.image_,
            gray_image,
            cv::COLOR_BGR2GRAY);

        cv::Mat binary_image;

        // 根据配置中的白色阈值提取候选白区域。
        cv::threshold(
            gray_image,
            binary_image,
            config.white_threshold_,
            255,
            cv::THRESH_BINARY);

        std::vector<std::vector<cv::Point>> contours;

        cv::findContours(
            binary_image,
            contours,
            cv::RETR_EXTERNAL,
            cv::CHAIN_APPROX_SIMPLE);

        std::size_t component_id = 0;

        for (const auto &contour : contours)
        {
            const double area =
                cv::contourArea(contour);

            // 过滤明显过小的噪声区域。
            // 具体阈值由合成数据测量后调整。
            if (area < config.min_area_)
            {
                continue;
            }

            WhiteComponent component;

            component.component_id_ = component_id++;

            component.contour_ = contour;

            component.area_ = area;

            component.bounding_box_ =
                cv::boundingRect(contour);

            const cv::Rect image_rect(
                0,
                0,
                frame.image_.cols,
                frame.image_.rows);

            // 判断白块是否接触工作图边缘。
            // 边界接触意味着该结构可能被裁切，
            // 后续 GeometryHypothesis 阶段需要降低可信度。
            component.touches_border_ =
                (component.bounding_box_.x <= image_rect.x) ||
                (component.bounding_box_.y <= image_rect.y) ||
                (component.bounding_box_.br().x >= image_rect.width) ||
                (component.bounding_box_.br().y >= image_rect.height);

            components.push_back(component);
        }

        return components;
    }

    // 将白色区域轮廓转换为结构化形状观测。
    // 输出 ShapeObservation，不负责决定最终 MARK 类别。
    std::vector<ShapeObservation> observeShapes(
        const std::vector<WhiteComponent> &components,
        const GeometryConfig &config)
    {
        std::vector<ShapeObservation> observations;

        for (const auto &component : components)
        {
            ShapeObservation observation;
            observation.source_component_id_ = component.component_id_;

            std::vector<cv::Point> approximated;

            // approxPolyDP 用于减少轮廓噪声，
            // 让后续凸凹结构分析基于稳定顶点。
            cv::approxPolyDP(
                component.contour_,
                approximated,
                config.approximation_epsilon_,
                true);

            for (const auto &point : approximated)
            {
                observation.simplified_polygon_.push_back(
                    cv::Point2f(
                        static_cast<float>(point.x),
                        static_cast<float>(point.y)));
            }

            const std::size_t vertex_count =
                observation.simplified_polygon_.size();

            if (vertex_count >= 3)
            {
                for (std::size_t i = 0;
                     i < vertex_count;
                     ++i)
                {
                    const auto &previous =
                        observation.simplified_polygon_
                            [(i + vertex_count - 1) % vertex_count];

                    const auto &current =
                        observation.simplified_polygon_[i];

                    const auto &next =
                        observation.simplified_polygon_
                            [(i + 1) % vertex_count];

                    TurnFeature feature;

                    feature.vertex_index_ = i;

                    feature.type_ =
                        classifyTurn(
                            previous,
                            current,
                            next);

                    // 当前 Block 2 规则：
                    // 使用第一个凹转折作为候选几何锚点。
                    // 如果没有凹点，不进行猜测，保持 nullopt。
                    if (feature.type_ == TurnType::CONCAVE &&
                        !observation.anchor_vertex_index_.has_value())
                    {
                        observation.anchor_vertex_index_ =
                            feature.vertex_index_;
                    }

                    observation.turns_.push_back(feature);
                }
            }

            /*
             * 当前阶段只保存观测误差接口。
             * 最终误差模型需要通过合成数据测量，
             * 不在 Block 2 提前假设。
             */
            // TODO: 待合成数据测量后填入
            observation.simplification_error_ = 0.0;

            /*
             * 初步类别支持：
             * 这里只提供候选，不做最终分类。
             *
             * 后续 GeometryHypothesis 会结合：
             * - 多个白片关系
             * - 仿射一致性
             * - 验证残差
             * 决定最终解释。
             */

            // 原来的6/7顶点分类会把圆角真L排除、把短M冒充L；改为完整轮廓显式L多候选。
            // 基础多边形/turns仅保留原始诊断口径，真正三L搜索消费以下六边结构候选。
            observation.l_topology_candidates_ = observeLTopologies(component, config);
            if (!observation.l_topology_candidates_.empty())
                observation.supported_classes_.push_back("L");
            else if (observation.turns_.size() >= 5)
                observation.supported_classes_.push_back("M");
            else
                observation.supported_classes_.push_back("S");

            observations.push_back(observation);
        }

        return observations;
    }

} // namespace mark