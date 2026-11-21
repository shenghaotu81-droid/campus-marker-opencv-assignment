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
        cv::Rect bounding_box_; // 能框住它的最小正矩形，快速知道它多大、在哪
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

        // cv::resize 使用像素中心映射；仅引导搜索，最终角必须从原图直接测量。（工作图（960×720）只用来找候选——检测快；但角点最后要在原图上精确测量（原图分辨率高））
        // 比较：cv::resize 变换的是图（像素数据）——把 1440×1080 的像素，重新采样成 960×720 的新图，产出了工作图；（把像素搬过去）
        //      original_to_work 变换的是坐标（两个数）——已知一个点在原图里的坐标，算出它在工作图里的坐标是多少；不碰任何像素，也不产出图，只是算数。（把点的“地址”换算过去）
        // 像素的坐标 (x, y) 指的是它的左上角，但像素的中心在 (x+0.5, y+0.5)
        // cv::resize是按像素中心来映射的，所以换算坐标得走中心：先 +0.5 拿到像素中心，再除（或乘）缩放比，最后 -0.5 回到左上角坐标
        // 工作图坐标 → 原图坐标（点对点坐标）
        cv::Point2d work_to_original(cv::Point2d point) const
        {
            return {(point.x + 0.5) / scale_x_ - 0.5, (point.y + 0.5) / scale_y_ - 0.5};
        }

        // 原图坐标 → 工作图坐标（点对点坐标）
        cv::Point2d original_to_work(cv::Point2d point) const
        {
            return {(point.x + 0.5) * scale_x_ - 0.5, (point.y + 0.5) * scale_y_ - 0.5};
        }

        // 原图坐标 -> 工作图坐标的缩放比例。
        // 像素中心偏移由上面的双向函数处理，不能直接乘比例。
        double scale_x_{1.0};

        double scale_y_{1.0};

        // 帧上下文。
        // 用于解码结果和输入帧关联。
        uint64_t frame_id_{0};

        int64_t timestamp_us_{0};
    };

} // namespace mark::detector
