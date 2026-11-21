#pragma once

#include <cstdint>
#include <vector>

#include <opencv2/core.hpp>

namespace mark::detector
{
    // 当前工作图实测白片；不携带模型身份、变换或跨帧状态。
    // 一个白块就是工作图里找出来的一个白色区域（这一帧里一个纯粹的几何形状，认身份、配对等是后面 match_quad 的事）
    struct WhiteComponent
    {
        std::vector<cv::Point> contour_; // 这个白块的轮廓，一串点围成的外形
        cv::Rect bounding_box_; // 能框住它的轴对齐外接矩形，右边和下边是开边界，快速知道它多大、在哪
        bool touches_border_{false}; // 它有没有碰到图边缘。碰到了就可能被裁切过，不完整，默认是 false。
    };

    // Detector preprocess 阶段输出。
    // 保存工作图以及从原图到工作图的显式坐标映射。
    // 结构匹配和原图拟合通过该结构共享坐标上下文。
    struct PreparedFrame
    {
        // preprocess 后的工作图。
        // 输出 resize 后的 BGR8 工作图，几何观察按阈值执行分割。
        cv::Mat image_;

        // 调用期原图浅视图，只供读取，不能原地修改或缓存跨帧。
        cv::Mat original_image_;
        int original_white_threshold_{-1};

        // 将工作图像素中心换算到原图，保留 resize 的半像素偏移。
        cv::Point2d work_to_original(cv::Point2d point) const
        {
            return {(point.x + 0.5) / scale_x_ - 0.5, (point.y + 0.5) / scale_y_ - 0.5};
        }

        double scale_x_{1.0};

        double scale_y_{1.0};
    };

} // namespace mark::detector
