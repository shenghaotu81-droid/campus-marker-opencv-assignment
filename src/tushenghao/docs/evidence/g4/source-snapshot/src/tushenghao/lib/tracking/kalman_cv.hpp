#pragma once

#include "mark/tracking_config.hpp"

namespace mark::tracking::detail
{
// 数学单元可独立测试；不用默认CV_32F，也不依赖OpenCV的简化协方差更新。
class CvFilter
{
public:
    void initialize(const cv::Vec3d &position, const cv::Matx33d &R, double velocity_variance);
    void bootstrap(const cv::Vec3d &previous, const cv::Matx33d &previous_R,
                   const cv::Vec3d &current, const cv::Matx33d &current_R,
                   double dt, double acceleration_psd, double dt_error_bound = 0,
                   double position_time_bound_s = 0);
    void predict(double dt, double acceleration_psd, double dt_error_bound = 0);
    double nis(const cv::Vec3d &measurement, const cv::Matx33d &R) const;
    void correct(const cv::Vec3d &measurement, const cv::Matx33d &R);
    cv::Vec3d position() const;
    cv::Vec3d velocity() const;
    double position_std() const;
    double velocity_std() const;
    const Mat6 &covariance() const noexcept { return P_; }

private:
    Vec6 x_{Vec6::all(0)};
    Mat6 P_{Mat6::zeros()};
};

bool finite_vector(const cv::Vec3d &value);
bool positive_definite(const cv::Mat &matrix);
cv::Matx33d position_block(const Mat6 &matrix);
} // namespace mark::tracking::detail
