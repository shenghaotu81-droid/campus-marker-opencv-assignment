// 按时间顺序推进轨迹；有位置量测则更新，无量测则预测，并保留数值失败前的状态。
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

        bool same_context(const pose::CaptureStamp &a, const pose::CaptureStamp &b)
        {
            return a.session_id_ == b.session_id_ && a.camera_id_ == b.camera_id_ && a.clock_domain_ == b.clock_domain_ && a.pixel_domain_ == b.pixel_domain_ && a.exposure_us_.has_value() == b.exposure_us_.has_value();
        }

        double time_bound(const pose::CaptureStamp &s, const TrackingConfig &c)
        {
            return c.stamp_uncertainty_semantics_ == "absolute_bound" && s.exposure_us_ && s.time_uncertainty_s_ ? *s.time_uncertainty_s_ : 0;
        }

    }
    struct State
    {
        detail::CvFilter filter_;
        bool live_ = false, confirmed_ = false;
        uint64_t id_ = 0, next_id_ = 1;
        int64_t state_time_ = 0;
        std::optional<pose::CaptureStamp> last_input_;
        pose::CaptureStamp last_measurement_;
        detail::PositionMeasurement seed_;
        TrackingResult advance(const TrackingFrame &f, const TrackingConfig &c)
        {
            TrackingResult out;
            out.input_stamp_ = f.stamp_;
            // 时间字段来自帧输入；排除负时刻、曝光倒置和非法误差界，防止污染滤波时间轴。
            const auto &stamp = f.stamp_;
            if (stamp.received_us_ < 0 ||
                (stamp.exposure_us_ && (*stamp.exposure_us_ < 0 || *stamp.exposure_us_ > stamp.received_us_)) ||
                (stamp.time_uncertainty_s_ && (!std::isfinite(*stamp.time_uncertainty_s_) || *stamp.time_uncertainty_s_ < 0)))
            {
                out.events_.push_back("INVALID_FRAME_STAMP");
                return out;
            }
            if (last_input_ && !same_context(*last_input_, f.stamp_))
            {
                live_ = confirmed_ = false;
                last_input_.reset();
                out.events_.push_back("CONTEXT_RESET");
            }
            if (last_input_ && (f.stamp_.frame_id_ <= last_input_->frame_id_ || detail::sample_time(f.stamp_) <= detail::sample_time(*last_input_) || f.stamp_.received_us_ < last_input_->received_us_))
            {
                out.events_.push_back("DUPLICATE_OR_REORDERED_FRAME");
                return out;
            }
            auto previous = last_input_;
            last_input_ = f.stamp_;
            out.input_accepted_ = true;
            if (!c.profile_)
                return out;
            const auto &p = *c.profile_;
            const auto now = detail::sample_time(f.stamp_);
            if (live_)
            {
                double dt = elapsed(now, state_time_), bound = previous ? time_bound(*previous, c) + time_bound(f.stamp_, c) : 0;
                filter_.predict(dt, p.acceleration_psd_m2ps3_, bound < dt ? bound : 0);
            }
            state_time_ = now;
            auto measurements = detail::adapt_measurements(f, c);
            for (auto &m:measurements)
            {
                double b = time_bound(f.stamp_, c);
                if (b > 0)
                {
                    cv::Vec3d v = live_ ? filter_.velocity() : cv::Vec3d::all(0);
                    cv::Matx33d vv = cv::Matx33d::eye() * p.initial_velocity_variance_m2ps2_;
                    if (live_)
                        for (int i = 0; i < 3; ++i)
                            for (int j = 0; j < 3; ++j)
                                vv(i, j) = filter_.covariance()(i + 3, j + 3);

                    m.R_ = detail::time_augmented_noise(m.R_, v, vv, b, p.acceleration_psd_m2ps3_);
                }
            }
            bool measured = false;
            if (!live_)
            {
                if (measurements.size() == 1)
                {
                    id_ = next_id_++;
                    seed_ = measurements.front();
                    filter_.initialize(seed_.position_, seed_.R_, p.initial_velocity_variance_m2ps2_);
                    last_measurement_ = f.stamp_;
                    live_ = measured = true;
                    confirmed_ = false;
                    out.events_.push_back("TRACK_CREATED");
                }
            }
            else if (!measurements.empty())
            {
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
                    if (!confirmed_)
                    {
                        double dt = elapsed(now, detail::sample_time(last_measurement_));
                        double bound = time_bound(last_measurement_, c) + time_bound(f.stamp_, c);
                        filter_.bootstrap(seed_.position_, seed_.R_, m.position_, m.R_, dt, p.acceleration_psd_m2ps3_, bound < dt ? bound : 0, bound < dt ? time_bound(f.stamp_, c) : 0);
                        confirmed_ = true;
                    }
                    else
                        filter_.correct(innovation);
                    seed_ = m;
                    last_measurement_ = f.stamp_;
                    measured = true;
                }
            }
            if (live_)
            {
                TargetTrack t;
                t.track_id_ = id_;
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
        explicit Impl(TrackingConfig c) : config_(std::move(c))
        {
        };
    };
    TargetTracker::TargetTracker(TrackingConfig c) : impl_(std::make_unique<Impl>(std::move(c)))
    {
    }

    TargetTracker::~TargetTracker() = default;
    TrackingResult TargetTracker::step(const TrackingFrame &f)
    {
        try{
            auto candidate = impl_->state_;
            auto out = candidate.advance(f, impl_->config_);
            if (out.input_accepted_)
                impl_->state_ = std::move(candidate);
            return out;
        }
        catch (const std::exception &e){
            TrackingResult out;
            out.input_stamp_ = f.stamp_;
            out.events_.push_back(std::string("TRACK_MATH_REJECTED_NO_COMMIT:") + e.what());
            return out;
        }
    }

    void TargetTracker::reset() noexcept
    {
        impl_->state_.live_ = impl_->state_.confirmed_ = false;
        impl_->state_.last_input_.reset();
    }

}
