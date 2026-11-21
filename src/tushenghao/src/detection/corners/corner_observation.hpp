#pragma once
#include "vision/types/prepared_frame_types.hpp"
#include "vision/config/detector_config.hpp"
#include <string>

namespace mark::detector
{
    // at_corner 的返回结果
    struct OriginalContourResult
    {
        // 指向找到的轮廓的指针，没找到就是 nullptr
        // 用指针而不是拷一份，是因为轮廓就存在 index 自己的 contours_ 里，直接指过去就行
        const std::vector<cv::Point> *contour_{nullptr};
        // 这条轮廓在 contours_ 里的下标，默认是 size_t 的最大值，当“没有”用的哨兵
        // std::numeric_limits<size_t>::max()：返回 size_t 类型的最大值
        size_t component_id_{std::numeric_limits<size_t>::max()};
        // 成功时为空，失败时填诊断字符串（比如找不到轮廓、轮廓被截断）
        std::string reason_;
    };

    // 原图轮廓索引index;这个 index 每帧新建、不跨帧复用；返回的 contour_ 指针只在这一帧的 decode_stage 期间有效；它只管“角附近有什么轮廓”，不知道 MARK 的部件模型
    // 一帧一索引，直接按四边形角查当前原图轮廓；不缓存跨帧、不访问部件模型。
    class OriginalContourIndex
    {
      public:
        // 构造时只记下 frame 的引用（原图在 PreparedFrame 里，index 不拥有它）
        explicit OriginalContourIndex(const PreparedFrame &);
        // 核心查询：给一个角点（原图坐标），返回离它最近的原图轮廓
        // 注意它不是 const，因为第一次调用时会触发懒准备，改状态
        OriginalContourResult at_corner(cv::Point2d);

      private:
        // 懒准备的开关
        // threshold 原图 + findContours 只在第一次 at_corner 时跑，且每帧至多一次
        // 如果这帧根本没有 match 走到精修，prepare 一次都不跑（省开销）
        void prepare();
        bool prepared_{false};

        const PreparedFrame &frame_; // 引用 PreparedFrame，index 不拥有它
        std::vector<std::vector<cv::Point>> contours_; // prepare 找出的原图轮廓列表
    };
}
