// 首量测初始化，下一次成功关联估计初速。轨迹随后逐帧预测，有合适量测时更新。
#pragma once
#include "filter/covariance_validation.hpp"
#include "vision/config/tracking_config.hpp"

namespace mark::tracking::detail
{
    // innovation() 返回残差 residual_、新息协方差 S_、量测噪声 R_ 和 NIS 分数 score_。分数用于排序，最小分数平局时跳过更新。
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
        // 下一次成功关联后，用两次位置量测的区间差分初始化速度。
        // 初始速度为 (p1−p0)/dt，P 保留 p–v 交叉项 r1/dt，速度块对角补 q·dt/3。
        void bootstrap(const cv::Vec3d &, const cv::Matx33d &, const cv::Vec3d &,
                       const cv::Matx33d &, double, double);
        // 预测到下一时刻。调用方保证 dt 为正且有限。
        // 连续白加速度 CV 模型的 Q 含 q·dt³/3、q·dt²/2 和 q·dt 三项，q 单位 m²/s³。
        void predict(double, double);
        // 拿一个量测跟预测值比，算残差、新息协方差 S、NIS 分数；只算不改状态（const 方法）
        // 残差为 z − 预测位置，S 为 P 的位置块加 R。用 Cholesky 求解计算 NIS = 残差ᵀS⁻¹残差。
        InnovationResult innovation(const cv::Vec3d &, const cv::Matx33d &) const;
        // 拿 innovation 的结果真正更新状态和协方差（卡尔曼更新步）
        // 用 Joseph 形式更新协方差，P⁺ = (I−KH)P⁻(I−KH)ᵀ + KRKᵀ。
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
