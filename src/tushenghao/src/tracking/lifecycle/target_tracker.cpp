// 按时间顺序推进一条轨迹，数值失败时保留上次提交的结果。
#include "vision/tracking/target_tracker.hpp"
#include "filter/kalman_cv.hpp"
#include "measurement/measurement_adapter.hpp"
#include <stdexcept>

namespace mark::tracking
{
    const char *track_state_name(TrackState s) noexcept
    {
        return s == TrackState::Measured ? "MEASURED" : "COASTING";
    }

    namespace
    {
        double elapsed(int64_t a, int64_t b)
        {
            return double((static_cast<long double>(a) - b) / 1e6L);
        }
    }

    struct State
    {
        detail::CvFilter filter_;

        bool live_ = false, confirmed_ = false;

        // 有 profile 且输入提交成功时才推进状态时钟。
        int64_t state_time_ = 0;

        std::optional<pose::CaptureStamp> last_input_;

        pose::CaptureStamp last_measurement_;

        // 首个量测仅供确认前的初速估计使用。
        detail::PositionMeasurement seed_;

        TrackingResult advance(const TrackingFrame &f, const TrackingConfig &c)
        {
            TrackingResult out;
            out.input_stamp_ = f.stamp_;

            const auto &stamp = f.stamp_;
            // 先拒绝负采样时间。
            if (stamp.received_us_ < 0)
            {
                out.events_.push_back("INVALID_FRAME_STAMP");
                return out;
            }

            // 要求帧号和采样时刻均严格递增。
            if (last_input_ &&
                (f.stamp_.frame_id_ <= last_input_->frame_id_ ||
                 detail::sample_time(f.stamp_) <= detail::sample_time(*last_input_)))
            {
                out.events_.push_back("DUPLICATE_OR_REORDERED_FRAME");
                return out;
            }

            // 记录当前输入。step 只提交成功推进的状态。
            last_input_ = f.stamp_;
            out.input_accepted_ = true;

            if (!c.profile_)
                return out;

            const auto &p = *c.profile_;
            const auto now = detail::sample_time(f.stamp_);

            // 有轨迹时先预测。未建轨时等待唯一量测。
            if (live_)
            {
                double dt = elapsed(now, state_time_);

                filter_.predict(dt, p.acceleration_psd_m2ps3_);
            }
            state_time_ = now;

            // 把来源匹配的位姿转成位置量测。
            auto measurements = detail::adapt_measurements(f, c);

            bool measured = false;

            if (!live_)
            {
                if (measurements.size() == 1)
                {
                    seed_ = measurements.front();
                    filter_.initialize(seed_.position_, seed_.R_,
                                       p.initial_velocity_variance_m2ps2_);

                    last_measurement_ = f.stamp_;
                    live_ = measured = true;
                    confirmed_ = false;
                    out.events_.push_back("TRACK_CREATED");
                }
            }
            else if (!measurements.empty())
            {
                // 按 NIS 选唯一最低分量测。最低分平局时沿用预测。
                size_t best = 0;
                auto innovation = filter_.innovation(measurements[0].position_, measurements[0].R_);
                bool tie = false;
                for (size_t i = 1; i < measurements.size(); ++i)
                {
                    auto item = filter_.innovation(measurements[i].position_, measurements[i].R_);
                    if (item.score_ < innovation.score_)
                    {
                        innovation = item;
                        best = i;
                        tie = false;
                    }
                    else if (item.score_ == innovation.score_)
                        tie = true;
                }

                if (!tie)
                {
                    auto &m = measurements[best];
                    out.association_nis_ = innovation.score_;

                    // 下一次成功关联用于差分初始化速度，之后用常规滤波更新。
                    if (!confirmed_)
                    {
                        double dt = elapsed(now, detail::sample_time(last_measurement_));
                        filter_.bootstrap(seed_.position_, seed_.R_, m.position_, m.R_, dt,
                                          p.acceleration_psd_m2ps3_);
                        confirmed_ = true;
                    }
                    else
                        filter_.correct(innovation);

                    last_measurement_ = f.stamp_;
                    measured = true;
                }
            }

            if (live_)
            {
                // 发布轨迹状态。速度在确认后才写出。
                TargetTrack t;

                t.state_ = measured ? TrackState::Measured : TrackState::Coasting;

                t.last_measurement_stamp_ = last_measurement_;

                t.state_time_us_ = now;

                t.position_m_ = filter_.position();
                t.covariance_ = filter_.covariance();

                if (confirmed_)
                    t.velocity_mps_ = filter_.velocity();
                out.tracks_.push_back(t);
            }
            return out;
        }
    };

    struct TargetTracker::Impl
    {
        TrackingConfig config_;
        State state_;

        explicit Impl(TrackingConfig c) : config_(std::move(c)) {};
    };

    TargetTracker::TargetTracker(TrackingConfig c) : impl_(std::make_unique<Impl>(std::move(c)))
    {
    }

    TargetTracker::~TargetTracker() = default;

    TrackingResult TargetTracker::step(const TrackingFrame &f)
    {
        try
        {
            // 在副本上推进，拒绝输入或数值异常时保留已提交状态。
            auto candidate = impl_->state_;
            auto out = candidate.advance(f, impl_->config_);

            if (out.input_accepted_)
                impl_->state_ = std::move(candidate);
            return out;
        }
        catch (const std::exception &e)
        {
            TrackingResult out;
            out.input_stamp_ = f.stamp_;

            out.events_.push_back(std::string("TRACK_MATH_REJECTED_NO_COMMIT:") + e.what());
            return out;
        }
    }
}
