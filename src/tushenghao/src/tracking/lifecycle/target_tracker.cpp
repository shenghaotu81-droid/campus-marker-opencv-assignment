#include "vision/tracking/target_tracker.hpp"
#include "filter/kalman_cv.hpp"
#include "measurement/measurement_adapter.hpp"
#include <limits>
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
        detail::CvFilter filter;
        bool live = false, confirmed = false;
        uint64_t id = 0, next_id = 1;
        int64_t state_time = 0;
        std::optional<pose::CaptureStamp> last_input;
        pose::CaptureStamp last_measurement;
        detail::PositionMeasurement seed;
        TrackingResult advance(const TrackingFrame &f, const TrackingConfig &c)
        {
            TrackingResult out;
            out.input_stamp_ = f.stamp_;
            if (!f.stamp_.valid())
            {
                out.events_.push_back("INVALID_FRAME_STAMP");
                return out;
            }
            if (last_input && !same_context(*last_input, f.stamp_))
            {
                live = confirmed = false;
                last_input.reset();
                out.events_.push_back("CONTEXT_RESET");
            }
            if (last_input && (f.stamp_.frame_id_ <= last_input->frame_id_ || detail::sample_time(f.stamp_) <= detail::sample_time(*last_input) || f.stamp_.received_us_ < last_input->received_us_))
            {
                out.events_.push_back("DUPLICATE_OR_REORDERED_FRAME");
                return out;
            }
            auto previous = last_input;
            last_input = f.stamp_;
            out.input_accepted_ = true;
            if (!c.profile_)
                return out;
            const auto &p = *c.profile_;
            const auto now = detail::sample_time(f.stamp_);
            if (live)
            {
                double dt = elapsed(now, state_time), bound = previous ? time_bound(*previous, c) + time_bound(f.stamp_, c) : 0;
                filter.predict(dt, p.acceleration_psd_m2ps3_, bound < dt ? bound : 0);
            }
            state_time = now;
            auto measurements = detail::adapt_measurements(f, c);
            for (auto &m:measurements)
            {
                double b = time_bound(f.stamp_, c);
                if (b > 0)
                {
                    cv::Vec3d v = live ? filter.velocity() : cv::Vec3d::all(0);
                    cv::Matx33d vv = cv::Matx33d::eye() * p.initial_velocity_variance_m2ps2_;
                    if (live)
                        for (int i = 0; i < 3; ++i)
                            for (int j = 0; j < 3; ++j)
                                vv(i, j) = filter.covariance()(i + 3, j + 3);

                    m.R = detail::time_augmented_noise(m.R, v, vv, b, p.acceleration_psd_m2ps3_);
                }
            }
            bool measured = false;
            if (!live)
            {
                if (measurements.size() == 1)
                {
                    if (next_id == std::numeric_limits<uint64_t>::max())
                        throw std::runtime_error("TRACK_ID_EXHAUSTED");
                    id = next_id++;
                    seed = measurements.front();
                    filter.initialize(seed.position, seed.R, p.initial_velocity_variance_m2ps2_);
                    last_measurement = f.stamp_;
                    live = measured = true;
                    confirmed = false;
                    out.events_.push_back("TRACK_CREATED");
                }
            }
            else if (!measurements.empty())
            {
                size_t best = 0;
                auto innovation = filter.innovation(measurements[0].position, measurements[0].R);
                bool tie = false;
                for (size_t i = 1; i < measurements.size(); ++i)
                {
                    auto item = filter.innovation(measurements[i].position, measurements[i].R);
                    if (item.score < innovation.score)
                    {
                        innovation = item;
                        best = i;
                        tie = false;
                    }
                    else if (item.score == innovation.score)
                        tie = true;
                }
                if (!tie)
                {
                    auto &m = measurements[best];
                    out.association_nis_ = innovation.score;
                    if (!confirmed)
                    {
                        double dt = elapsed(now, detail::sample_time(last_measurement));
                        double bound = time_bound(last_measurement, c) + time_bound(f.stamp_, c);
                        filter.bootstrap(seed.position, seed.R, m.position, m.R, dt, p.acceleration_psd_m2ps3_, bound < dt ? bound : 0, bound < dt ? time_bound(f.stamp_, c) : 0);
                        confirmed = true;
                    }
                    else
                        filter.correct(innovation);
                    seed = m;
                    last_measurement = f.stamp_;
                    measured = true;
                }
            }
            if (live)
            {
                TargetTrack t;
                t.track_id_ = id;
                t.state_ = measured ? TrackState::Measured : TrackState::Coasting;
                t.last_measurement_stamp_ = last_measurement;
                t.state_time_us_ = now;
                t.position_m_ = filter.position();
                t.covariance_ = filter.covariance();
                if (confirmed)
                    t.velocity_mps_ = filter.velocity();
                out.tracks_.push_back(t);
            }
            return out;
        }

    };
    struct TargetTracker::Impl
    {
        TrackingConfig config;
        State state;
        explicit Impl(TrackingConfig c) : config(std::move(c))
        {
        };
    };
    TargetTracker::TargetTracker(TrackingConfig c) : impl_(std::make_unique<Impl>(std::move(c)))
    {
        validate_tracking_config(impl_->config);
    }

    TargetTracker::~TargetTracker() = default;
    TrackingResult TargetTracker::step(const TrackingFrame &f)
    {
        try{
            auto candidate = impl_->state;
            auto out = candidate.advance(f, impl_->config);
            if (out.input_accepted_)
                impl_->state = std::move(candidate);
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
        impl_->state.live = impl_->state.confirmed = false;
        impl_->state.last_input.reset();
    }

}
