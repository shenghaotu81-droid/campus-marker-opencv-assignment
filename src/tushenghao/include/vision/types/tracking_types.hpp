#pragma once
#include <vector>
#include "vision/types/pose_types.hpp"

namespace mark::tracking
{
    // 类型别名
    // OpenCV 只预定义到 4 维（Vec2d、Vec3d、Vec4d、Matx33d 等）
    // Opencv的矩阵/向量
    // Vec/Matx 是“小而固定”的（栈上、编译时定尺寸），Mat 是“大而灵活”的（堆上、运行时定尺寸）；卡尔曼的状态（6 维）和协方差（6×6）尺寸固定，所以用 Vec6/Mat6；图像尺寸不固定，所以用 Mat
    // cv::Vec<T, n>——固定长度的向量（一维）；大小写在模板参数里，编译时就定死了，存在栈上，不碰堆内存，速度快；Vec3d（= Vec<double, 3>）就是三维点，Vec6 就是 6 维状态
    // cv::Matx<T, m, n>——固定大小的矩阵（二维）；同样编译时定尺寸、栈上分配；Matx33d（= Matx<double, 3, 3>）就是 3×3 矩阵（R 和相机内参 K 那种），Mat6 就是 6×6 协方差
    // cv::Mat——通用动态矩阵；行列在运行时才定，数据在堆上（引用计数）；图像、大矩阵、尺寸不固定的都用它；K 是 Mat（因为 solvePnP 要 Mat），R是 Matx33d（因为卡尔曼数学是固定 3×3）。
    using Vec6 = cv::Vec<double, 6>;
    using Mat6 = cv::Matx<double, 6, 6>;
    enum class TrackState
    {
        Measured,
        Coasting
    };

    struct TrackingFrame
    {
        pose::CaptureStamp stamp_;
        std::vector<pose::PoseObservation> observations_;
    };

    struct TargetTrack
    {
        uint64_t track_id_{};
        TrackState state_{TrackState::Coasting};
        pose::CaptureStamp last_measurement_stamp_;
        int64_t state_time_us_{};
        // 有轨迹即有完整数值状态；首样本尚不能测速度。
        cv::Vec3d position_m_{0, 0, 0};
        Mat6 covariance_{Mat6::zeros()};
        std::optional<cv::Vec3d> velocity_mps_;
    };

    struct TrackingResult
    {
        pose::CaptureStamp input_stamp_;
        bool input_accepted_{false}; // 标记这一帧的输入是否被接受（通过时间戳检查、上下文检查、乱序检查等）
        std::vector<TargetTrack> tracks_;
        std::vector<std::string> events_;
        std::optional<double> association_nis_;
    };

    const char *track_state_name(TrackState) noexcept;
}
