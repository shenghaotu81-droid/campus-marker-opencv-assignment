#include "measurement/measurement_adapter.hpp"
#include "filter/kalman_cv.hpp"
#include <set>
#include <stdexcept>
namespace mark::tracking::detail
{
    int64_t sample_time(const pose::CaptureStamp &s)
    {
        return s.exposure_us_.value_or(s.received_us_);
    }

    bool same_sample(const pose::CaptureStamp &a, const pose::CaptureStamp &b)
    {
        return a.session_id_ == b.session_id_ && a.camera_id_ == b.camera_id_ && a.clock_domain_ == b.clock_domain_ && a.pixel_domain_ == b.pixel_domain_ && a.frame_id_ == b.frame_id_ && a.received_us_ == b.received_us_ && a.exposure_us_ == b.exposure_us_ && a.time_uncertainty_s_ == b.time_uncertainty_s_;
    }

    cv::Matx33d time_augmented_noise(const cv::Matx33d &r, const cv::Vec3d &v, const cv::Matx33d &vv, double b, double q)
    {
        if (!std::isfinite(b) || b < 0 || !std::isfinite(q) || q < 0)
            throw std::runtime_error("TRACK_TIME_NOISE_INVALID");
        if (b == 0)
            return r;
        return 2 * (r + b * b * 2 * (vv + v * v.t()) + cv::Matx33d::eye() * (q * b * b * b / 3));
    }

    std::vector<PositionMeasurement> adapt_measurements(const TrackingFrame &f, const TrackingConfig &c)
    {
        std::vector<PositionMeasurement> out;
        std::set<size_t> seen, duplicates;
        for (const auto &p:f.observations_)
            if (p.source_ == c.source_ && p.target_id_ == c.target_class_ && p.measurement())
                if (!seen.insert(p.detection_index_).second)
                    duplicates.insert(p.detection_index_);

        for (const auto &p:f.observations_)
        {
            if (p.source_ != c.source_ || p.target_id_ != c.target_class_ || duplicates.count(p.detection_index_) || !same_sample(p.stamp_, f.stamp_))
                continue;
            if (p.status_ != pose::PoseStatus::Unique && p.status_ != pose::PoseStatus::Ambiguous)
                continue;
            const auto *selected = p.measurement();
            if (!selected || !finite_vector(selected->pose_.t_m_) || selected->pose_.t_m_[2] <= 0 || !std::isfinite(selected->rmse_px_) || selected->rmse_px_ < 0)
                continue;
            PositionMeasurement m{selected->pose_.t_m_, c.profile_->camera_mse_m2_};
            bool valid = true;
            // 连续误差权重，没有差距/质量资格门。等误差时等权，劣支误差越大影响越小。
            // 这是相对残差模型，不冒充标定过的后验概率。
            for (size_t i = 0; i < p.candidates_.size(); ++i)
                if (i != *p.selected_index_)
                {
                    const auto &other = p.candidates_[i];
                    if (!finite_vector(other.pose_.t_m_) || other.pose_.t_m_[2] <= 0 || !std::isfinite(other.rmse_px_) || other.rmse_px_ < 0)
                    {
                        valid = false;
                        break;
                    }
                    const double scale = std::max(selected->rmse_px_, other.rmse_px_);
                    const double a = scale > 0 ? selected->rmse_px_ / scale : 1, b = scale > 0 ? other.rmse_px_ / scale : 1;
                    const double weight = a * a / (a * a + b * b);
                    auto delta = other.pose_.t_m_ - m.position;
                    m.R += weight * (delta * delta.t());
                }
            if (valid)
                out.push_back(m);
        }
        return out;
    }

}
