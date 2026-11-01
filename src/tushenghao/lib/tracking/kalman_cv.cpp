#include "tracking/kalman_cv.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace mark::tracking::detail
{
namespace
{
// 这里只处理浮点舍入对称性，不给物理预算添加隐形epsilon或对角“救场”噪声。
Mat6 checked_covariance(const cv::Mat &matrix)
{
    const cv::Mat symmetric = (matrix + matrix.t()) * 0.5;
    if (!positive_definite(symmetric))
        throw std::runtime_error("TRACK_COVARIANCE_NOT_POSITIVE_DEFINITE");
    Mat6 result;
    symmetric.copyTo(cv::Mat(result, false));
    return result;
}

double block_std(const Mat6 &p, int start)
{
    cv::Matx33d block;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            block(i, j) = p(i + start, j + start);
    cv::Mat values;
    if (!cv::eigen(cv::Mat(block), values) || values.at<double>(0) < 0)
        throw std::runtime_error("TRACK_INVALID_STD_BLOCK");
    return std::sqrt(values.at<double>(0));
}
} // namespace

bool finite_vector(const cv::Vec3d &v)
{
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}


cv::Matx33d position_block(const Mat6 &p)
{
    cv::Matx33d out;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            out(i, j) = p(i, j);
    return out;
}

// 首帧速度只是零均值先验，不对外发布“测得零速”；方差来自独立数据二阶矩。
void CvFilter::initialize(const cv::Vec3d &p, const cv::Matx33d &r, double vv)
{
    if (!finite_vector(p) || !positive_definite(cv::Mat(r)) || !std::isfinite(vv) || vv <= 0)
        throw std::runtime_error("TRACK_BAD_INITIAL_STATE");
    ++revision_;
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

// 两个不同时刻才可观测速度；保留p-v交叉项，并补连续白加速度的端点速度误差。
void CvFilter::bootstrap(const cv::Vec3d &p0, const cv::Matx33d &r0,
                         const cv::Vec3d &p1, const cv::Matx33d &r1, double dt, double q, double bound, double position_bound)
{
    if (!finite_vector(p0) || !finite_vector(p1) || !positive_definite(cv::Mat(r0)) ||
        !positive_definite(cv::Mat(r1)) || !std::isfinite(dt) || dt <= 0 ||
        !std::isfinite(q) || q < 0 || !std::isfinite(bound) || bound < 0 || bound >= dt ||
        !std::isfinite(position_bound) || position_bound < 0)
        throw std::runtime_error("TRACK_BAD_BOOTSTRAP");
    ++revision_;
    const auto v = (p1 - p0) / dt;
    if (!finite_vector(v))
        throw std::runtime_error("TRACK_BOOTSTRAP_VELOCITY_OVERFLOW");
    if (bound > 0)
    {
        // 时间绝对界不是sigma。用最小可能dt限制除法误差，再用块MSE的2倍包络处理未知相关。
        const double lower = dt - bound;
        const auto velocity_mse = 2 * (2 * (r0 + r1) / (lower * lower) +
            (v * v.t()) * (bound * bound / (lower * lower))) +
            cv::Matx33d::eye() * (q * (dt + bound) / 3);
        P_ = Mat6::zeros();
        for (int i = 0; i < 3; ++i)
        {
            x_[i] = p1[i];
            x_[i + 3] = v[i];
            for (int j = 0; j < 3; ++j)
            {
                // R2：原有界初始化遗漏观测时偏导致的位置位移项。
                // 单帧界来自profile；用本次bootstrap自身速度二阶矩估计，不借旧track速度。
                P_(i, j) = 2 * (r1(i, j) + position_bound * position_bound *
                    (v[i] * v[j] + velocity_mse(i, j)));
                P_(i + 3, j + 3) = 2 * velocity_mse(i, j);
            }
        }
        P_ = checked_covariance(cv::Mat(P_));
        return;
    }
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
        P_(i + 3, i + 3) += q * dt / 3;
    }
    P_ = checked_covariance(cv::Mat(P_));
}

// 实际dt的连续白加速度CV模型；q单位m²/s³，不与离散常加速度dt⁴模型混用。
void CvFilter::predict(double dt, double q, double bound)
{
    if (!std::isfinite(dt) || dt < 0 || !std::isfinite(q) || q < 0 ||
        !std::isfinite(bound) || bound < 0 || (bound > 0 && dt <= bound))
        throw std::runtime_error("TRACK_BAD_PREDICTION_INTERVAL");
    Mat6 f = Mat6::eye(), noise = Mat6::zeros();
    const double upper = dt + bound;
    for (int i = 0; i < 3; ++i)
    {
        f(i, i + 3) = dt;
        noise(i, i) = q * upper * upper * upper / 3;
        noise(i, i + 3) = noise(i + 3, i) = q * upper * upper / 2;
        noise(i + 3, i + 3) = q * upper;
    }
    Mat6 propagated = f * P_ * f.t() + noise;
    if (bound > 0)
    {
        // Young不等式控制未知时偏与状态误差的相关项；bound/dt由量纲给出，不是经验调参。
        Mat6 a = Mat6::zeros();
        for (int i = 0; i < 3; ++i) a(i, i + 3) = 1;
        const double ratio = bound / dt;
        const auto second_moment_bound = 2 * (P_ + x_ * x_.t());
        propagated = (1 + ratio) * (f * P_ * f.t()) + noise +
            (1 + 1 / ratio) * bound * bound * (a * second_moment_bound * a.t());
    }
    ++revision_;
    x_ = f * x_;
    for (double value : x_.val)
        if (!std::isfinite(value)) throw std::runtime_error("TRACK_PREDICTION_STATE_OVERFLOW");
    P_ = checked_covariance(cv::Mat(propagated));
}

// 用Cholesky解线性方程，不显式求逆；创新门槛由未截断的正例分布标定。
InnovationResult CvFilter::innovation(const cv::Vec3d &z, const cv::Matx33d &r) const
{
    if (!finite_vector(z) || !positive_definite(cv::Mat(r)))
        throw std::runtime_error("TRACK_BAD_MEASUREMENT_NOISE");
    const auto innovation = z - position();
    const auto s = position_block(P_) + r;
    cv::Mat solved;
    if (!cv::solve(cv::Mat(s), cv::Mat(innovation), solved, cv::DECOMP_CHOLESKY))
        throw std::runtime_error("TRACK_NIS_SOLVE_FAILED");
    const double score = cv::Mat(innovation).dot(solved);
    if (!std::isfinite(score) || score < 0)
        throw std::runtime_error("TRACK_INVALID_NIS");
    return {innovation, s, r, score, this, revision_};
}

double CvFilter::nis(const cv::Vec3d &z,const cv::Matx33d &r) const
{
    return innovation(z,r).score;
}

// Joseph更新比(I-KH)P更能保持数值对称/正定；它仍依赖声明的线性噪声模型。
void CvFilter::correct(const cv::Vec3d &z, const cv::Matx33d &r)
{
    correct(innovation(z,r));
}

void CvFilter::correct(const InnovationResult &innovation)
{
    if (innovation.owner != this || innovation.revision != revision_)
        throw std::runtime_error("TRACK_STALE_INNOVATION");
    const auto &r = innovation.R;
    cv::Mat h = cv::Mat::zeros(3, 6, CV_64F);
    for (int i = 0; i < 3; ++i)
        h.at<double>(i, i) = 1;
    cv::Mat p(P_), solved;
    const cv::Mat s(innovation.S);
    const cv::Mat pht = p * h.t();
    if (!cv::solve(s, pht.t(), solved, cv::DECOMP_CHOLESKY))
        throw std::runtime_error("TRACK_GAIN_SOLVE_FAILED");
    const cv::Mat gain = solved.t();
    const cv::Mat state = cv::Mat(x_) + gain * cv::Mat(innovation.residual);
    if (!cv::checkRange(state)) throw std::runtime_error("TRACK_CORRECTION_STATE_OVERFLOW");
    ++revision_;
    state.copyTo(cv::Mat(x_, false));
    const cv::Mat a = cv::Mat::eye(6, 6, CV_64F) - gain * h;
    P_ = checked_covariance(a * p * a.t() + gain * cv::Mat(r) * gain.t());
}

cv::Vec3d CvFilter::position() const { return {x_[0], x_[1], x_[2]}; }
cv::Vec3d CvFilter::velocity() const { return {x_[3], x_[4], x_[5]}; }
double CvFilter::position_std() const { return block_std(P_, 0); }
double CvFilter::velocity_std() const { return block_std(P_, 3); }
} // namespace mark::tracking::detail
