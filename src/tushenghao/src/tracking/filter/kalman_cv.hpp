// 卡尔曼滤波：预测+更新 advance 驱动：predict → innovation → correct/bootstrap/initialize；把 position()/velocity()/covariance() 读出来装进 TargetTrack
// 轨迹的生命周期：initialize → bootstrap → predict → innovation → correct
// 第一帧量测来了 initialize，第二帧 bootstrap 算出速度，之后每帧先 predict 再 innovation/correct
#pragma once
#include "filter/covariance_validation.hpp"
#include "vision/config/tracking_config.hpp"

namespace mark::tracking::detail
{
    // innovation() 的返回值：residual_（量测 - 预测值）、S_（新息协方差）、R_（量测噪声）、score_（NIS 分数，拿来挑量测、做门控）
    struct InnovationResult
    {
        cv::Vec3d residual_;
        cv::Matx33d S_, R_;
        double score_{};
    };

    class CvFilter
    {
      public:
        // 第一次量测来了，用它初始化滤波器（位置 = 量测量，速度 = 0，P 按 R 和初始速度方差设）
        // 位置 = 量测；速度 = 零均值先验（不是“测出来速度是 0”），方差 vv；P 位置块 = R，速度对角 = vv
        void initialize(const cv::Vec3d &, const cv::Matx33d &, double);
        // 第二次量测来了，用两次量测算出初始速度（位置差 / 时间差），把轨迹“确认”
        // 速度 = (p1−p0)/dt，两个不同时刻才让速度可观；P 保留 p–v 交叉项（r1/dt），速度方差加 q·dt/3；bound>0 时用 dt−bound 当最小可能 dt
        void bootstrap(const cv::Vec3d &, const cv::Matx33d &, const cv::Vec3d &,
                       const cv::Matx33d &, double, double);
        // 往前推 dt（预测步）
        // 匀速模型，q 单位 m²/s³；P 按 q·dt³/3、q·dt²/2 的模式增长；bound>0 时加 Young 不等式项
        void predict(double, double);
        // 拿一个量测跟预测值比，算残差、新息协方差 S、NIS 分数；只算不改状态（const 方法）
        // 残差 = z − 预测位置；S = 位置块(P_) + R；NIS = 残差ᵀS⁻¹残差，Cholesky 解线性方程、不显式求逆；返回的 InnovationResult 带 owner/revision，防复用旧结果
        InnovationResult innovation(const cv::Vec3d &, const cv::Matx33d &) const;
        // 拿 innovation 的结果真正更新状态和协方差（卡尔曼更新步）
        // Joseph 形式 (I−KH)P(I−KH)ᵀ + KRKᵀ；owner/revision 对不上就拒绝，stale 的结果不让用
        void correct(const InnovationResult &);
        // 读出当前的位置（x_ 前 3 个）和速度（x_ 后 3 个）
        cv::Vec3d position() const;
        cv::Vec3d velocity() const;

        // 读出 P_（直接 return，noexcept）
        const Mat6 &covariance() const noexcept
        {
            return P_;
        }

      private:
      // 状态：x_ 是 Vec6（位置+速度，全 0 起）；P_ 是 Mat6（全 0 起）
      // position()/velocity() 就是读 x_ 的前 3 个 / 后 3 个；position_std()/velocity_std() 是 3×3 块最大特征值开方
        // x_（Vec6，初始全 0）——滤波器的状态
        // 6 维：前 3 个是位置（x、y、z），后 3 个是速度（vx、vy、vz）
        // 这是滤波器对“目标现在在哪、以多快在动”的估计
        Vec6 x_{Vec6::all(0)};
        // P_（Mat6，初始全 0）——6×6 协方差矩阵
        // 表示“这个估计有多不确定”
        // 对角线是各维的方差，非对角线是各维之间的相关性
        Mat6 P_{Mat6::zeros()};
    };

    // 自由函数helper：检查向量是否有限
    bool finite_vector(const cv::Vec3d &);
}
