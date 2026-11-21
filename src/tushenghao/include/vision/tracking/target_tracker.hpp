// 卡尔曼跟踪器的类：TargetTracker 每帧吃一个 TrackingFrame（帧时间戳 + 这一帧的位姿观测列表），返回一个 TrackingResult（轨迹 + 事件）
#pragma once
#include "vision/config/tracking_config.hpp"
#include <memory>

namespace mark::tracking
{
    // 和 Detector、TagDetector 同款结构（pimpl + 禁拷贝），PoseEstimator 是例外（无状态不用）
    class TargetTracker
    {
      public:
        // 构造函数，按值拿一份 TrackingConfig（tracker 拥有它）：explicit 防隐式转换
        explicit TargetTracker(TrackingConfig);
        // 析构函数，声明在头文件、定义在 .cpp（= default）；pimpl 的原因
        ~TargetTracker();
        // 禁止拷贝构造和拷贝赋值。TargetTracker 独占一个 Impl，不能复制，也顺带禁了移动（声明了析构 + 拷贝，编译器不生成移动操作）；造出来就固定在原地
        TargetTracker(const TargetTracker &) = delete;
        TargetTracker &operator=(const TargetTracker &) = delete;
        // 主入口：每帧调一次
        // 输入 TrackingFrame（const 引用：帧时间戳 + 观测列表），返回 TrackingResult（按值：轨迹 + 事件）
        // 注意没有 const——会改 tracker 自己的状态（提交新的滤波状态）
        // 实现是“先复制再提交”：先把当前状态拷一份给 candidate，在 candidate 上推进一帧；只有这一帧的输入被接受（input_accepted_ 为真）才把 candidate 移回 impl_->state_；失败或抛异常就丢弃 candidate、保留旧状态，并返回一个带 "TRACK_MATH_REJECTED_NO_COMMIT" 事件的结果（坏帧不会毒化 tracker）
        TrackingResult step(const TrackingFrame &);

      private:
        // struct Impl; + std::unique_ptr<Impl> impl_;——和 Detector、TagDetector 一样的 pimpl 手法
        // Impl 内部：config_（TrackingConfig）和 state_（State——卡尔曼滤波器 + 轨迹状态：live_/confirmed_ 标志、帧号、last_input_、last_measurement_、seed_）
        // 让这个类“有状态”——滤波器要逐帧演进，必须跨帧记住，所以 tracker 必须构造一次、循环里复用
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
