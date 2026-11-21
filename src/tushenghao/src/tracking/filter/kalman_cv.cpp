// 按匀速模型预测，用位置量测修正位置、速度及协方差。
#include "filter/covariance_validation.hpp"
#include "filter/kalman_cv.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace mark::tracking::detail
{
    namespace
    {
        // 先消除舍入造成的不对称，协方差不正定时抛错。
        Mat6 checked_covariance(const cv::Mat &matrix)
        {
            const cv::Mat symmetric = (matrix + matrix.t()) * 0.5;

            if (!positive_definite(symmetric))
                throw std::runtime_error("TRACK_COVARIANCE_NOT_POSITIVE_DEFINITE");
            Mat6 result;

            // 显式 false 让 Mat 引用定长矩阵，copyTo 直接写入结果。
            symmetric.copyTo(cv::Mat(result, false));
            return result;
        }
    }

    bool finite_vector(const cv::Vec3d &v)
    {
        return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
    }

    // 首个量测给出位置。速度用零均值先验。
    void CvFilter::initialize(const cv::Vec3d &p, const cv::Matx33d &r, double vv)
    {
        if (!finite_vector(p) || !positive_definite(cv::Mat(r)))
            throw std::runtime_error("TRACK_BAD_INITIAL_STATE");

        x_ = Vec6::all(0);

        P_ = Mat6::zeros();

        for (int i = 0; i < 3; ++i)
        {
            x_[i] = p[i];

            for (int j = 0; j < 3; ++j)
                P_(i, j) = r(i, j);

            P_(i + 3, i + 3) = vv;
        }
    }

    // 下一次成功关联用两次量测的区间差分估计初速。
    void CvFilter::bootstrap(const cv::Vec3d &p0, const cv::Matx33d &r0, const cv::Vec3d &p1,
                             const cv::Matx33d &r1, double dt, double q)
    {
        if (!finite_vector(p0) || !finite_vector(p1) || !positive_definite(cv::Mat(r0)) ||
            !positive_definite(cv::Mat(r1)) || !std::isfinite(dt) || dt <= 0)
            throw std::runtime_error("TRACK_BAD_BOOTSTRAP");

        // 两次量测的时刻不同，速度由位置差除以时间差。
        const auto v = (p1 - p0) / dt;

        if (!finite_vector(v))
            throw std::runtime_error("TRACK_BOOTSTRAP_VELOCITY_OVERFLOW");

        P_ = Mat6::zeros();
        for (int i = 0; i < 3; ++i)
        {
            x_[i] = p1[i];
            x_[i + 3] = v[i];

            for (int j = 0; j < 3; ++j)
            {
                P_(i, j) = r1(i, j);

                P_(i, j + 3) = P_(j + 3, i) = r1(i, j) / dt;

                P_(i + 3, j + 3) = (r0(i, j) + r1(i, j)) / (dt * dt);
            }

            // 补上区间平均速度相对末端速度的白加速度方差。
            P_(i + 3, i + 3) += q * dt / 3;
        }

        P_ = checked_covariance(cv::Mat(P_));
    }

    // 调用方保证 dt 为正且有限。
    void CvFilter::predict(double dt, double q)
    {
        // 构造匀速状态转移 F 和连续白加速度过程噪声 Q。
        Mat6 f = Mat6::eye(), noise = Mat6::zeros();
        for (int i = 0; i < 3; ++i)
        {
            f(i, i + 3) = dt;
            noise(i, i) = q * dt * dt * dt / 3;
            noise(i, i + 3) = noise(i + 3, i) = q * dt * dt / 2;
            noise(i + 3, i + 3) = q * dt;
        }

        Mat6 propagated = f * P_ * f.t() + noise;

        x_ = f * x_;

        for (double value : x_.val)
            if (!std::isfinite(value))
                throw std::runtime_error("TRACK_PREDICTION_STATE_OVERFLOW");

        P_ = checked_covariance(cv::Mat(propagated));
    }

    InnovationResult CvFilter::innovation(const cv::Vec3d &z, const cv::Matx33d &r) const
    {
        if (!finite_vector(z) || !positive_definite(cv::Mat(r)))
            throw std::runtime_error("TRACK_BAD_MEASUREMENT_NOISE");

        // 残差是量测位置减预测位置。
        const auto innovation = z - position();

        // 只观测位置，新息协方差为先验 P 的位置块加量测噪声 R。
        cv::Matx33d position_covariance;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                position_covariance(i, j) = P_(i, j);
        const auto s = position_covariance + r;

        // 用 Cholesky 求残差的平方马氏距离，供关联排序。
        cv::Mat solved;

        if (!cv::solve(cv::Mat(s), cv::Mat(innovation), solved, cv::DECOMP_CHOLESKY))
            throw std::runtime_error("TRACK_NIS_SOLVE_FAILED");

        const double score = cv::Mat(innovation).dot(solved);

        if (!std::isfinite(score) || score < 0)
            throw std::runtime_error("TRACK_INVALID_NIS");

        return {innovation, s, r, score};
    }

    void CvFilter::correct(const InnovationResult &innovation)
    {
        const auto &r = innovation.R_;
        cv::Mat h = cv::Mat::zeros(3, 6, CV_64F);
        for (int i = 0; i < 3; ++i)
            h.at<double>(i, i) = 1;

        cv::Mat p(P_), solved;
        const cv::Mat s(innovation.S_);
        // 使用转置后的右端求卡尔曼增益，避免显式求逆。
        const cv::Mat pht = p * h.t();

        if (!cv::solve(s, pht.t(), solved, cv::DECOMP_CHOLESKY))
            throw std::runtime_error("TRACK_GAIN_SOLVE_FAILED");
        const cv::Mat gain = solved.t();

        const cv::Mat state = cv::Mat(x_) + gain * cv::Mat(innovation.residual_);
        if (!cv::checkRange(state))
            throw std::runtime_error("TRACK_CORRECTION_STATE_OVERFLOW");
        // 校验后通过共享 Vec 存储的 Mat 写回状态。
        state.copyTo(cv::Mat(x_, false));

        // Joseph 形式有利于数值稳定。更新后对称化，再复核正定性。
        const cv::Mat a = cv::Mat::eye(6, 6, CV_64F) - gain * h;
        P_ = checked_covariance(a * p * a.t() + gain * cv::Mat(r) * gain.t());
    }

    cv::Vec3d CvFilter::position() const
    {
        return {x_[0], x_[1], x_[2]};
    }

    cv::Vec3d CvFilter::velocity() const
    {
        return {x_[3], x_[4], x_[5]};
    }
}
