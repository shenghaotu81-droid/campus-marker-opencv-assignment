// AprilTag 检测阶段的类（可选）：TagDetector 在图像里找 AprilTag（调 AprilTag C 库），返回检到的 tag（TagObservation：id、边长、4 个角点、时间戳）
#pragma once
#include <vector>
#include <memory>
#include "vision/pose/pose_estimator.hpp"

namespace mark::pose
{
    class TagDetector
    {
      public:
        // 构造函数，按值拿一份 TagConfig（allowed_ids + side_m），detector 拥有它；explicit 防隐式转换。
        explicit TagDetector(TagConfig);
        // 析构函数，声明在头文件、定义在 .cpp；pimpl 的原因：析构时要 delete Impl，必须在 Impl 完整定义的地方
        ~TagDetector();
        // 禁止拷贝构造和拷贝赋值。TagDetector 独占一个 Impl，不能复制，也顺带禁了移动（不会自动生成）（“一处拥有、不可复制”的对象）
        TagDetector(const TagDetector &) = delete;
        TagDetector &operator=(const TagDetector &) = delete;
        // 主入口：输入一张图（const cv::Mat &，要求 BGR 彩色图）和一个 CaptureStamp，返回这一帧里检到的所有 tag（没检到就返回空 vector）
        // 注意：此处没有 const（不像 PoseEstimator 的方法）——因为 detect 会改内部状态（AprilTag C 库的 detector 有内部缓冲/状态，每次 detect 都会复用和修改它）
        // 实现：先校验图（非空、CV_8UC3、2D），转灰度，调 apriltag_detector_detect，再按 allowed_ids 白名单过滤（不在白名单里的 tag 跳过），最后包成 TagObservation
        std::vector<TagObservation> detect(const cv::Mat &, const CaptureStamp &);

      private:
        // struct Impl; + std::unique_ptr<Impl> impl_;——和 Detector 一样的 pimpl 手法
        // Impl 内部：config_（TagConfig）、family_（tag36h11 的 family）、detector_（AprilTag detector）
        // Impl 的构造函数里配 detector 参数（单线程、不降采样、不模糊、精修边缘、锐化 0.25）并注册 tag36h11 family；创建失败抛 TAG_ALLOCATION_FAILED
        struct Impl;
        std::unique_ptr<Impl> impl_;  // 必要性：Impl 里用的是 AprilTag C 库的类型（apriltag_family_t、apriltag_detector_t）和 C 函数，避免C 库依赖泄漏
    };

    // 类外自由函数（不用 TagDetector 的状态（Impl/config），只用 tag 观测 + 求解器；纯转换（TagObservation → PoseObservation））：
    // 输入一个 TagObservation（检到的 tag）、cv::Size（图像尺寸）、const PoseEstimator &（位姿求解器），返回 PoseObservation（tag 的位姿）
    // 实现：先看 tag 有没有有效边长（side_m_ 有限且 > 0）：没有 → 返回一个没有 estimate 的 PoseObservation，reason_ = "METRIC_SIZE_MISSING_OR_INVALID"（“未知尺寸不猜”——没尺寸就拒绝）；有 → 用 side_m/2 = h 造 tag 的 4 个 3D 模型点（一个边长 side_m、中心在原点的正方形：(-h,h,0)、(h,h,0)、(h,-h,0)、(-h,-h,0)），调 estimator.estimate_planar(...)——和 MARK 一样的通用平面 solvePnP——再把 source_ 改成 "APRILTAG"
    // 用途：退出流程——tag 的位姿给 退出状态用（看到 TAG → 后退 → 退出自动）
    PoseObservation estimate_exit(const TagObservation &, cv::Size, const PoseEstimator &);
}
