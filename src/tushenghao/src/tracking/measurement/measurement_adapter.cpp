// 把同帧、同身份的合法名义位置转为量测，基础噪声叠加位置分歧矩阵。
#include "measurement/measurement_adapter.hpp"
#include "filter/kalman_cv.hpp"
#include <set>
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
        if (b == 0)
            return r;
        return 2 * (r + b * b * 2 * (vv + v * v.t()) + cv::Matx33d::eye() * (q * b * b * b / 3));
    }

    std::vector<PositionMeasurement> adapt_measurements(const TrackingFrame &f, const TrackingConfig &c)
    {
        std::vector<PositionMeasurement> out;
        std::set<size_t> seen, duplicates;
        for (const auto &p:f.observations_)
            if (p.source_ == c.source_ && p.target_id_ == c.target_class_ && p.estimate_)
                if (!seen.insert(p.detection_index_).second)
                    duplicates.insert(p.detection_index_);

        for (const auto &p:f.observations_)
        {
            if (p.source_ != c.source_ || p.target_id_ != c.target_class_ || duplicates.count(p.detection_index_) || !same_sample(p.stamp_, f.stamp_))
                continue;
            const auto &measurement = p.estimate_;
            if (!measurement || !finite_vector(measurement->pose_.t_m_) || measurement->pose_.t_m_[2] <= 0 ||
                !cv::checkRange(cv::Mat(measurement->translation_spread_m2_)))
                continue;
            out.push_back({measurement->pose_.t_m_,
                c.profile_->camera_mse_m2_ + measurement->translation_spread_m2_});
        }
        return out;
    }

}
