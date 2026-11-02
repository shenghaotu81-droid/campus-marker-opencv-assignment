#include "vision/math/covariance_validation.hpp"
#include "measurement/measurement_adapter.hpp"
#include "filter/kalman_cv.hpp"
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

namespace mark::tracking::detail
{
std::int64_t sample_time(const pose::CaptureStamp &s)
{
    return s.exposure_us_.value_or(s.received_us_);
}

bool valid_stamp(const pose::CaptureStamp &s)
{
    return !s.session_id_.empty() && !s.camera_id_.empty() && !s.clock_domain_.empty() &&
        s.pixel_domain_ == pose::PixelDomain::RawDistorted && s.received_us_ >= 0 &&
        (!s.exposure_us_ || (*s.exposure_us_ >= 0 && *s.exposure_us_ <= s.received_us_)) &&
        (!s.time_uncertainty_s_ || (std::isfinite(*s.time_uncertainty_s_) && *s.time_uncertainty_s_ >= 0));
}

// 不只比较frame_id，避免同编号的旧会话或其他相机结果混进当前测量。
bool same_sample(const pose::CaptureStamp &a, const pose::CaptureStamp &b)
{
    return a.session_id_ == b.session_id_ && a.camera_id_ == b.camera_id_ &&
        a.clock_domain_ == b.clock_domain_ && a.pixel_domain_ == b.pixel_domain_ &&
        a.frame_id_ == b.frame_id_ && a.received_us_ == b.received_us_ &&
        a.exposure_us_ == b.exposure_us_ && a.time_uncertainty_s_ == b.time_uncertainty_s_;
}

// time_uncertainty不是σ；仅在旁路语义明确为绝对界且处于标定域时授予时间资格。
bool time_bounded(const TrackingFrame &f, const TrackingConfig &c)
{
    return f.origin_ != InputOrigin::LegacyG2Replay &&
        c.stamp_uncertainty_semantics_ == "absolute_bound" && c.max_time_uncertainty_s_ &&
        c.profile_ && c.profile_->validated_time_bound_s_ && f.stamp_.exposure_us_ &&
        f.stamp_.time_uncertainty_s_ &&
        *f.stamp_.time_uncertainty_s_ <= *c.max_time_uncertainty_s_ &&
        *f.stamp_.time_uncertainty_s_ <= *c.profile_->validated_time_bound_s_;
}

// 有界时偏产生的位置MSE包络：显式保留状态二阶矩/过程项，未知相关用Cauchy的2倍和。
// 它是局部CV模型的包络，不是“time_uncertainty=高斯sigma”，不宣称实车绝对误差保证。
cv::Matx33d time_augmented_noise(const cv::Matx33d &r, const cv::Vec3d &velocity,
    const cv::Matx33d &velocity_covariance, double b, double q)
{
    if (b == 0) return r;
    if (!std::isfinite(b) || b < 0 || !std::isfinite(q) || q < 0)
        throw std::runtime_error("TRACK_TIME_NOISE_BOUND_INVALID");
    const auto velocity_second_bound = 2 * (velocity_covariance + velocity * velocity.t());
    const auto time_mse = b * b * velocity_second_bound + cv::Matx33d::eye() * (q * b * b * b / 3);
    return 2 * (r + time_mse);
}

namespace
{
// 变换只接收严格刚体；sqrt(eps)是SO(3)浮点检查，不是实物安装误差预算。
bool valid_transform(const CameraTransform &t, const TrackingFrame &f, const TrackingConfig &c)
{
    const auto &r = t.R_world_camera_;
    const double eps = std::sqrt(std::numeric_limits<double>::epsilon());
    if (!same_sample(t.stamp_, f.stamp_) || t.world_frame_id_ != c.world_frame_id_ ||
        !finite_vector(t.t_world_camera_m_) || !cv::checkRange(cv::Mat(r)) ||
        cv::norm(cv::Mat(r.t() * r - cv::Matx33d::eye())) > eps ||
        std::abs(cv::determinant(cv::Mat(r)) - 1) > eps)
        return false;
    if (t.covariance_)
    {
        cv::Mat values;
        const cv::Mat covariance(*t.covariance_);
        if (!cv::checkRange(covariance) || cv::norm(covariance - covariance.t()) > eps ||
            !cv::eigen(covariance, values) || values.at<double>(5) < 0)
            return false;
    }
    return true;
}

// 不猜SE3可选数组的排列；相机位置误差用独立PositionNoise或标定Profile。
std::optional<PositionMeasurement> one_measurement(
    const pose::PoseObservation &p, const TrackingFrame &f,
    const TrackingConfig &c, std::vector<std::string> &events)
{
    if (p.source_ != c.source_ || p.target_id_ != c.target_class_)
        return std::nullopt;
    if (!same_sample(p.stamp_, f.stamp_) || p.status_ != pose::PoseStatus::Unique ||
        !p.T_camera_target_ || !p.selected_index_ || *p.selected_index_ >= p.candidates_.size())
    {
        events.push_back("POSE_NOT_A_CURRENT_UNIQUE_MEASUREMENT");
        return std::nullopt;
    }
    const auto &candidate = p.candidates_[*p.selected_index_];
    std::size_t eligible_count = 0;
    for (const auto &item : p.candidates_)
        eligible_count += item.eligible_;
    if (eligible_count != 1 || !candidate.eligible_ || !candidate.metrics_valid_ || !candidate.reasons_.empty() ||
        candidate.min_depth_m_ <= 0 || candidate.rmse_px_ < 0 || candidate.max_error_px_ < 0 ||
        !std::isfinite(candidate.min_depth_m_) || !std::isfinite(candidate.rmse_px_) ||
        !std::isfinite(candidate.max_error_px_) || !finite_vector(candidate.pose_.rvec_) ||
        !finite_vector(candidate.pose_.t_m_) || !finite_vector(p.T_camera_target_->rvec_) ||
        !finite_vector(p.T_camera_target_->t_m_) || p.T_camera_target_->t_m_[2] <= 0)
    {
        events.push_back("POSE_SELECTED_EVIDENCE_INVALID");
        return std::nullopt;
    }
    PositionMeasurement m;
    m.detection_index_ = p.detection_index_;
    m.position_m_ = p.T_camera_target_->t_m_;
    m.R_m2_ = c.profile_->camera_mse_m2_;
    m.hardware_verified_ = p.control_usable_ && c.profile_->hardware_verified_ &&
        f.origin_ == InputOrigin::TypedMemory;
    std::size_t matched = 0;
    for (const auto &noise : f.position_noise_)
        if (noise.detection_index_ == p.detection_index_)
        {
            ++matched;
            if (noise.evidence_id_.empty() || !positive_definite(cv::Mat(noise.camera_mse_m2_)))
            {
                events.push_back("POSITION_NOISE_INVALID");
                return std::nullopt;
            }
            m.R_m2_ = noise.camera_mse_m2_;
            m.hardware_verified_ = m.hardware_verified_ && noise.hardware_verified_;
        }
    if (matched > 1)
    {
        events.push_back("POSITION_NOISE_AMBIGUOUS_BINDING");
        return std::nullopt;
    }
    if (c.coordinate_mode_ == CoordinateMode::LocalWorld)
    {
        if (!f.camera_transform_ || !valid_transform(*f.camera_transform_, f, c))
        {
            events.push_back("WORLD_TRANSFORM_MISSING_OR_INVALID");
            return std::nullopt;
        }
        const auto &t = *f.camera_transform_;
        const auto rotated = t.R_world_camera_ * m.position_m_;
        m.position_m_ = rotated + t.t_world_camera_m_;
        m.R_m2_ = t.R_world_camera_ * m.R_m2_ * t.R_world_camera_.t();
        if (t.covariance_)
        {
            const cv::Matx33d skew(0, -rotated[2], rotated[1], rotated[2], 0, -rotated[0],
                                   -rotated[1], rotated[0], 0);
            cv::Matx<double, 3, 6> j = cv::Matx<double, 3, 6>::zeros();
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 3; ++col)
                {
                    j(row, col) = row == col ? 1 : 0;
                    j(row, col + 3) = -skew(row, col);
                }
            // 未假定视觉误差与ego独立：Cov(a+b) <= 2(Cov(a)+Cov(b))，仍是局部线性模型。
            m.R_m2_ = 2 * (m.R_m2_ + j * *t.covariance_ * j.t());
        }
        else
        {
            events.push_back("WORLD_EGO_UNCERTAINTY_UNKNOWN_CONDITIONAL_ONLY");
            m.hardware_verified_ = false;
        }
        m.hardware_verified_ = m.hardware_verified_ && t.hardware_verified_;
    }
    if (!positive_definite(cv::Mat(m.R_m2_)))
        return std::nullopt;
    return m;
}
} // namespace

std::vector<PositionMeasurement> adapt_measurements(
    const TrackingFrame &f, const TrackingConfig &c, std::vector<std::string> &events)
{
    std::vector<PositionMeasurement> out;
    std::map<std::size_t, int> index_count;
    for (const auto &p : f.observations_)
        if (p.source_ == c.source_ && p.target_id_ == c.target_class_ &&
            p.status_ == pose::PoseStatus::Unique)
            ++index_count[p.detection_index_];
    for (const auto &p : f.observations_)
    {
        if (index_count[p.detection_index_] > 1)
        {
            events.push_back("DUPLICATE_DETECTION_INDEX");
            continue;
        }
        auto m = one_measurement(p, f, c, events);
        if (m)
            out.push_back(*m);
    }
    return out;
}
} // namespace mark::tracking::detail
