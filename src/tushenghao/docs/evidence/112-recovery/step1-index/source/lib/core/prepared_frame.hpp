/*实现范围：预处理后的当前帧数据
FrameInput
    |
    v
preprocess()
    |
    +-- resize
    |
    +-- threshold
    |
    +-- coordinate mapping
    |
    +-- frame context
    |
    v
PreparedFrame
*/
#pragma once

#include <cstdint>
#include <vector>

#include "core/geometry_types.hpp"

#include <opencv2/core.hpp>

namespace mark
{

    // Detector preprocess 阶段输出。
    // 保存工作图以及从原图到工作图的显式坐标映射。
    // Block2 后续只消费该结构，不负责恢复映射关系。
    struct PreparedFrame
    {
        // preprocess 后的工作图。
        // 当前阶段输出 resize 后的BGR8工作图，二值化由原三L观测模块负责。
        cv::Mat image_;

        // G1：调用期原图浅视图；所有消费者只能读，不能原地 threshold/绘图或缓存跨帧。
        // 旧实现仅保留 resize 图，除 scale 不能恢复已经丢失的原图边证据。
        cv::Mat original_image_;
        int original_white_threshold_{-1};

        // cv::resize 使用像素中心映射；仅引导搜索，最终角必须从原图直接测量。
        cv::Point2d workToOriginal(cv::Point2d point) const
        {
            return {(point.x + 0.5) / scale_x_ - 0.5,
                    (point.y + 0.5) / scale_y_ - 0.5};
        }
        cv::Point2d originalToWork(cv::Point2d point) const
        {
            return {(point.x + 0.5) * scale_x_ - 0.5,
                    (point.y + 0.5) * scale_y_ - 0.5};
        }

        // 原图坐标 -> 工作图坐标的缩放比例。
        // working_x = original_x * scale_x_
        // working_y = original_y * scale_y_
        double scale_x_{1.0};

        double scale_y_{1.0};

        /**
         * @brief 当前帧白色连通区域
         *
         * Block 2 分割阶段生成。
         *
         * Block 3 根据 GeometryHypothesis 中的 component_id_
         * 直接回查这里的真实观测轮廓。
         * 加 components_ 以补这条链：component_id_ → 按ID查找frame.components_ → contour_
         *
         * 不重新分割，避免产生第二套观测结果。
         */
        std::vector<WhiteComponent> components_;

        // 帧上下文。
        // 用于后续阶段 audit 和结果关联。
        uint64_t frame_id_{0};

        int64_t timestamp_us_{0};
    };

} // namespace mark
