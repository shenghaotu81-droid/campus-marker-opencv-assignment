// 按配置来源取位置量测，噪声由基础 R 和位姿分歧组成。
#include "measurement/measurement_adapter.hpp"
#include "filter/kalman_cv.hpp"
#include <set>

namespace mark::tracking::detail
{
    // 离线使用帧号与 FPS 推算的微秒时刻。
    int64_t sample_time(const pose::CaptureStamp &s)
    {
        return s.received_us_;
    }

    std::vector<PositionMeasurement> adapt_measurements(const TrackingFrame &f,
                                                        const TrackingConfig &c)
    {
        std::vector<PositionMeasurement> out;
        // 在来源匹配且有估计的观测中找重复检测索引。
        std::set<size_t> seen, duplicates;

        for (const auto &p : f.observations_)
            if (p.source_ == c.source_ && p.estimate_)

                if (!seen.insert(p.detection_index_).second)

                    duplicates.insert(p.detection_index_);

        for (const auto &p : f.observations_)
        {
            // 排除不同来源及重复观测，再检查本次消费的数值。
            if (p.source_ != c.source_ || duplicates.count(p.detection_index_))
                continue;
            const auto &measurement = p.estimate_;

            if (!measurement || !finite_vector(measurement->pose_.t_m_) ||
                measurement->pose_.t_m_[2] <= 0 ||
                !cv::checkRange(cv::Mat(measurement->translation_spread_m2_)))
                continue;

            out.push_back({measurement->pose_.t_m_,
                           c.profile_->camera_mse_m2_ + measurement->translation_spread_m2_});
        }

        return out;
    }
}
