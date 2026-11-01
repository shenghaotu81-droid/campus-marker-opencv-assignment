#include "mark/target_tracker.hpp"
#include "tracking/kalman_cv.hpp"
#include "tracking/measurement_adapter.hpp"
#include <limits>
#include <stdexcept>

namespace mark::tracking
{
const char *track_state_name(TrackState state) noexcept
{
    switch (state)
    {
    case TrackState::Measured: return "MEASURED";
    case TrackState::Coasting: return "COASTING";
    case TrackState::Lost: return "LOST";
    }
    return "LOST";
}

namespace
{
double elapsed(std::int64_t newer, std::int64_t older)
{
    return static_cast<double>((static_cast<long double>(newer) - older) / 1e6L);
}

// 曝光/接收时钟不能中途混用；同名模型不是实例，track_id只标记本模块连续估计区间。
bool same_context(const pose::CaptureStamp &a, const pose::CaptureStamp &b)
{
    return a.session_id_ == b.session_id_ && a.camera_id_ == b.camera_id_ &&
        a.clock_domain_ == b.clock_domain_ && a.pixel_domain_ == b.pixel_domain_ &&
        a.exposure_us_.has_value() == b.exposure_us_.has_value();
}
} // namespace

// K02→K09：只读配置与可变状态分开；候选 State 成功后一次提交。
struct TrackerState
{
    detail::CvFilter filter_;
    bool live_{false};
    bool confirmed_{false};
    bool all_time_bounded_{false};
    bool current_time_bounded_{false};
    bool all_measurements_verified_{false};
    std::uint64_t next_id_{1};
    std::uint64_t id_{0};
    std::optional<pose::CaptureStamp> last_input_;
    pose::CaptureStamp last_measurement_;
    std::int64_t state_time_us_{0};
    detail::PositionMeasurement seed_;
    std::optional<detail::PositionMeasurement> pending_;
    pose::CaptureStamp pending_stamp_;
    std::optional<TargetTrack> lost_;
    TrackState state_{TrackState::Lost};


    // reset不复用旧ID；无持久物理ID的前提下，重建必须能被下游察觉。
    void clear() noexcept
    {
        live_ = false;
        confirmed_ = false;
        pending_.reset();
        lost_.reset();
        last_input_.reset();
        state_ = TrackState::Lost;
    }

    std::string frame_name(const TrackingConfig &config) const
    {
        return config.coordinate_mode_ == CoordinateMode::LocalWorld ?
            config.world_frame_id_ : "camera-relative:" + last_measurement_.camera_id_;
    }

    // 首帧只测得位置；速度留空。这里的零均值仅是滤波先验，绝不发布零速事实。
    void create(const detail::PositionMeasurement &m, const TrackingFrame &f,
                std::vector<std::string> &events, const TrackingConfig &config)
    {
        if (next_id_ == std::numeric_limits<std::uint64_t>::max())
            throw std::runtime_error("TRACK_ID_EXHAUSTED");
        id_ = next_id_++;
        filter_.initialize(m.position_m_, m.R_m2_, config.profile_->initial_velocity_variance_m2ps2_);
        seed_ = m;
        last_measurement_ = f.stamp_;
        state_time_us_ = detail::sample_time(f.stamp_);
        live_ = true;
        confirmed_ = false;
        all_time_bounded_ = current_time_bounded_;
        all_measurements_verified_ = m.hardware_verified_ && f.origin_ == InputOrigin::TypedMemory;
        state_ = TrackState::Measured;
        lost_.reset();
        pending_.reset();
        events.push_back("TRACK_CREATED:" + std::to_string(id_));
    }

    TargetTrack snapshot(const TrackingConfig &config) const
    {
        TargetTrack t;
        t.track_id_ = id_;
        t.state_ = state_;
        t.source_ = config.source_;
        t.target_class_ = config.target_class_;
        t.coordinate_frame_ = frame_name(config);
        t.velocity_meaning_ = config.coordinate_mode_ == CoordinateMode::LocalWorld ?
            "TARGET_VELOCITY_IN_LOCAL_WORLD" : "CAMERA_COORDINATE_RATE_INCLUDES_EGO_MOTION";
        t.last_measurement_stamp_ = last_measurement_;
        t.last_detection_index_ = seed_.detection_index_;
        t.state_time_us_ = state_time_us_;
        t.confirmed_ = confirmed_;
        t.time_bounded_ = all_time_bounded_ && current_time_bounded_;
        t.model_hardware_verified_ = all_measurements_verified_ && config.profile_->hardware_verified_;
        t.input_time_bound_s_ = last_input_ ? last_input_->time_uncertainty_s_ : std::nullopt;
        t.uncertainty_scope_ = config.time_policy_ == "bounded_only" && t.time_bounded_ ?
            "LOCAL_LINEAR_MSE_ENVELOPE_WITH_TIME_BOUNDS" : "MODEL_CONDITIONAL_ON_NOMINAL_TIME";
        t.observation_age_s_ = elapsed(state_time_us_, detail::sample_time(last_measurement_));
        if (live_)
        {
            t.position_m_ = filter_.position();
            t.covariance_ = filter_.covariance();
            if (confirmed_ && filter_.velocity_std() <= config.profile_->velocity_std_limit_mps_)
                t.velocity_mps_ = filter_.velocity();
            t.qualified_for_g5_ = state_ == TrackState::Measured && t.velocity_mps_ &&
                filter_.position_std() <= config.profile_->position_std_limit_m_ &&
                all_time_bounded_ && t.model_hardware_verified_ && config.time_policy_ == "bounded_only";
        }
        return t;
    }

    // 失效按误差增长/标定时间域，不按随意“丢5帧”；LOST记录清空旧位置与速度。
    void lose(const std::string &reason, std::vector<std::string> &events, const TrackingConfig &config)
    {
        live_ = false;
        state_ = TrackState::Lost;
        lost_ = snapshot(config);
        lost_->confirmed_ = false;
        events.push_back("TRACK_LOST:" + std::to_string(id_) + ":" + reason);
    }

    // 时间界不能被当标准差；区间下界<=0时不做两点速度除法。
    bool interval_resolved(const pose::CaptureStamp &previous, const TrackingFrame &f, const TrackingConfig &config) const
    {
        if (config.time_policy_ != "bounded_only")
            return true;
        return previous.time_uncertainty_s_ && f.stamp_.time_uncertainty_s_ &&
            elapsed(detail::sample_time(f.stamp_), detail::sample_time(previous)) >
            *previous.time_uncertainty_s_ + *f.stamp_.time_uncertainty_s_;
    }

    // 两点候选独立于旧滤波中心；旧模型失配时可重新建立，避免随机反转被永久拒绝。
    bool reseed(const detail::PositionMeasurement &m, const TrackingFrame &f,
                std::vector<std::string> &events, const TrackingConfig &config)
    {
        const auto &p = *config.profile_;
        if (pending_)
        {
            const double dt = elapsed(detail::sample_time(f.stamp_), detail::sample_time(pending_stamp_));
            if (dt > 0 && dt <= p.max_dt_s_ && interval_resolved(pending_stamp_, f, config))
            {
                detail::CvFilter candidate;
                candidate.initialize(pending_->position_m_, pending_->R_m2_, p.initial_velocity_variance_m2ps2_);
                const double bound = config.time_policy_ == "bounded_only" ?
                    *f.stamp_.time_uncertainty_s_ + *pending_stamp_.time_uncertainty_s_ : 0;
                candidate.predict(dt, p.acceleration_psd_m2ps3_, bound);
                if (candidate.nis(m.position_m_, m.R_m2_) <= p.bootstrap_nis_limit_)
                {
                    const auto previous = *pending_;
                    const auto previous_stamp = pending_stamp_;
                    events.push_back("TRACK_RESEEDED_NEW_ID_OLD:" + std::to_string(id_));
                    create(m, f, events, config);
                    // profile已在构造时验证；有界测量还须通过time_bounded的非空检查。
                    filter_.bootstrap(previous.position_m_, previous.R_m2_, m.position_m_, m.R_m2_,
                                      dt, p.acceleration_psd_m2ps3_, bound,
                                      bound > 0 ? *p.validated_time_bound_s_ : 0);
                    confirmed_ = true;
                    all_time_bounded_ = all_time_bounded_ && previous_stamp.exposure_us_ &&
                        previous_stamp.time_uncertainty_s_;
                    all_measurements_verified_ = all_measurements_verified_ && previous.hardware_verified_;
                    return true;
                }
            }
        }
        pending_ = m;
        pending_stamp_ = f.stamp_;
        events.push_back("PENDING_RESEED_POSITION_ONLY");
        return false;
    }

    TrackingResult advance(const TrackingFrame &f, const TrackingConfig &config)
    {
        TrackingResult result;
        result.input_stamp_ = f.stamp_;
        result.ready_ = config.profile_.has_value();
        if (!detail::valid_stamp(f.stamp_))
        {
            result.events_.push_back("INVALID_FRAME_STAMP");
            return result;
        }
        if (last_input_ && !same_context(*last_input_, f.stamp_))
        {
            clear();
            result.events_.push_back("CONTEXT_CHANGED_RESET");
        }
        if (last_input_ && (f.stamp_.frame_id_ <= last_input_->frame_id_ ||
            detail::sample_time(f.stamp_) <= detail::sample_time(*last_input_) ||
            f.stamp_.received_us_ < last_input_->received_us_))
        {
            result.events_.push_back("DUPLICATE_REORDERED_OR_ZERO_DT_REJECTED");
            return result;
        }
        const auto previous_input = last_input_;
        const bool previous_time_bounded = current_time_bounded_;
        result.input_accepted_ = true;
        last_input_ = f.stamp_;
        current_time_bounded_ = detail::time_bounded(f, config);
        if (!result.ready_)
        {
            result.events_.push_back("TRACKING_PROFILE_NOT_READY");
            return result;
        }
        const auto &p = *config.profile_;
        const auto process_mode = config.coordinate_mode_ == CoordinateMode::LocalWorld ? "local_world" : "camera_relative";
        const bool profile_frame_matches = p.coordinate_mode_ == process_mode && p.time_policy_ == config.time_policy_ &&
            p.camera_id_ == f.stamp_.camera_id_;
        if (!profile_frame_matches)
            result.events_.push_back("PROFILE_PROCESS_FRAME_UNVALIDATED_RESEARCH_ONLY");
        auto measurements = detail::adapt_measurements(f, config, result.events_);
        if (config.time_policy_ == "bounded_only" && !current_time_bounded_)
        {
            measurements.clear();
            result.events_.push_back("TIME_QUALITY_UNKNOWN_OR_OUTSIDE_VALIDATED_DOMAIN");
        }
        const auto now = detail::sample_time(f.stamp_);
        if (live_)
        {
            const double dt = elapsed(now, state_time_us_);
            state_time_us_ = now;
            if (config.time_policy_ == "bounded_only" && current_time_bounded_ && !previous_time_bounded)
                lose("TIME_DOMAIN_REACQUIRED_REBUILD", result.events_, config);
            else if (dt > p.max_dt_s_)
                lose("STEP_OUTSIDE_CALIBRATED_DT_DOMAIN", result.events_, config);
            else
            {
                double bound = 0;
                if (config.time_policy_ == "bounded_only" && current_time_bounded_ &&
                    previous_time_bounded && previous_input)
                    bound = *f.stamp_.time_uncertainty_s_ + *previous_input->time_uncertainty_s_;
                filter_.predict(dt, p.acceleration_psd_m2ps3_, bound);
            }
        }
        for (auto &m : measurements)
        {
            m.hardware_verified_ = m.hardware_verified_ && profile_frame_matches;
            if (config.time_policy_ == "bounded_only" && current_time_bounded_ &&
                *f.stamp_.time_uncertainty_s_ > 0)
            {
                // 绝对时偏界产生位移误差MSE包络，绝不把界直接当高斯sigma。
                const double b = *f.stamp_.time_uncertainty_s_;
                cv::Vec3d velocity(0, 0, 0);
                cv::Matx33d velocity_second = cv::Matx33d::eye() * p.initial_velocity_variance_m2ps2_;
                if (live_)
                {
                    velocity = filter_.velocity();
                    for (int i = 0; i < 3; ++i)
                        for (int j = 0; j < 3; ++j)
                            velocity_second(i, j) = filter_.covariance()(i + 3, j + 3);
                }
                m.R_m2_ = detail::time_augmented_noise(m.R_m2_, velocity, velocity_second,
                                                       b, p.acceleration_psd_m2ps3_);
            }
        }
        if (!live_)
        {
            if (measurements.size() == 1)
                create(measurements.front(), f, result.events_, config);
            else if (measurements.size() > 1)
                result.events_.push_back("INITIAL_TARGET_COMPETITION_NO_SELECTION");
        }
        else
        {
            std::vector<std::pair<std::size_t,detail::InnovationResult>> accepted;
            const double gate = confirmed_ ? p.association_nis_limit_ : p.bootstrap_nis_limit_;
            for (std::size_t i = 0; i < measurements.size(); ++i)
            {
                auto innovation = filter_.innovation(measurements[i].position_m_, measurements[i].R_m2_);
                if (innovation.score <= gate)
                    accepted.emplace_back(i,std::move(innovation));
            }
            bool updated = false;
            if (accepted.size() == 1 && interval_resolved(last_measurement_, f, config))
            {
                const auto &m = measurements[accepted.front().first];
                result.association_nis_ = accepted.front().second.score;
                if (!confirmed_)
                {
                    const double dt = elapsed(now, detail::sample_time(last_measurement_));
                    filter_.bootstrap(seed_.position_m_, seed_.R_m2_, m.position_m_, m.R_m2_,
                                      dt, p.acceleration_psd_m2ps3_,
                        config.time_policy_ == "bounded_only" && all_time_bounded_ && current_time_bounded_ ?
                        *f.stamp_.time_uncertainty_s_ + *last_measurement_.time_uncertainty_s_ : 0,
                        config.time_policy_ == "bounded_only" && all_time_bounded_ && current_time_bounded_ ?
                        *p.validated_time_bound_s_ : 0);
                    confirmed_ = true;
                }
                else
                    filter_.correct(accepted.front().second);
                last_measurement_ = f.stamp_;
                seed_ = m;
                all_time_bounded_ = all_time_bounded_ && current_time_bounded_;
                all_measurements_verified_ = all_measurements_verified_ && m.hardware_verified_ &&
                    f.origin_ == InputOrigin::TypedMemory;
                state_ = TrackState::Measured;
                pending_.reset();
                updated = true;
            }
            else if (accepted.empty() && measurements.size() == 1)
                updated = reseed(measurements.front(), f, result.events_, config);
            else
            {
                pending_.reset();
                if (accepted.size() > 1)
                    result.events_.push_back("ASSOCIATION_COMPETITION_COAST_ONLY");
            }
            if (!updated)
                state_ = TrackState::Coasting;
            // 预测沿用域只终止COASTING，不能删除当前有效测量/阻止初始化收敛。
            // 当前MEASURED不够精确时保留估计和P，但qualified_for_g5仍为false。
            if (!updated && filter_.position_std() > p.position_std_limit_m_)
                lose("POSITION_UNCERTAINTY_EXCEEDED", result.events_, config);
        }
        if (live_)
            result.tracks_.push_back(snapshot(config));
        else if (lost_)
        {
            auto lost = *lost_;
            lost.state_time_us_ = now;
            lost.observation_age_s_ = elapsed(now, detail::sample_time(lost.last_measurement_stamp_));
            result.tracks_.push_back(lost);
        }
        return result;
    }
};

struct TargetTracker::Impl {
    const TrackingConfig config_;
    TrackerState state_;
    explicit Impl(TrackingConfig config):config_(std::move(config)) {}
};

TargetTracker::TargetTracker(TrackingConfig config) : impl_(std::make_unique<Impl>(std::move(config)))
{
    validate_tracking_config(impl_->config_);
}

TargetTracker::~TargetTracker() = default;

// 按值事务：坏帧/数学失败不污染内部时间或轨迹，下一合法帧仍能继续。
TrackingResult TargetTracker::step(const TrackingFrame &frame)
{
    try
    {
        auto candidate = impl_->state_;
        auto result = candidate.advance(frame, impl_->config_);
        if (result.input_accepted_)
            impl_->state_ = std::move(candidate);
        return result;
    }
    catch (const std::exception &error)
    {
        TrackingResult result;
        result.input_stamp_ = frame.stamp_;
        result.ready_ = impl_->config_.profile_.has_value();
        result.events_.push_back(std::string("TRACK_MATH_REJECTED_NO_COMMIT:") + error.what());
        return result;
    }
}

// 查询未来状态不推进滤波历史、不产生新测量；G5自行决定查询的作用时刻。
TargetForecast TargetTracker::predict_to(std::int64_t time) const
{
    TargetForecast out;
    if (!impl_->state_.live_ || !impl_->state_.confirmed_)
    {
        out.reason_ = "NO_CONFIRMED_TRACK";
        return out;
    }
    const double dt = elapsed(time, impl_->state_.state_time_us_);
    if (dt < 0)
    {
        out.reason_ = "FORECAST_BEFORE_CURRENT_STATE";
        return out;
    }
    auto filter = impl_->state_.filter_;
    filter.predict(dt, impl_->config_.profile_->acceleration_psd_m2ps3_);
    if (filter.position_std() > impl_->config_.profile_->position_std_limit_m_ ||
        filter.velocity_std() > impl_->config_.profile_->velocity_std_limit_mps_)
    {
        out.reason_ = "FORECAST_UNCERTAINTY_EXCEEDED";
        return out;
    }
    out.available_ = true;
    out.track_id_ = impl_->state_.id_;
    out.coordinate_frame_ = impl_->state_.frame_name(impl_->config_);
    out.session_id_ = impl_->state_.last_measurement_.session_id_;
    out.clock_domain_ = impl_->state_.last_measurement_.clock_domain_;
    out.time_bounded_ = impl_->state_.all_time_bounded_ && impl_->state_.current_time_bounded_;
    out.state_time_us_ = time;
    out.last_measurement_us_ = detail::sample_time(impl_->state_.last_measurement_);
    out.position_m_ = filter.position();
    out.velocity_mps_ = filter.velocity();
    out.covariance_ = filter.covariance();
    out.reason_ = "MODEL_CONDITIONAL_PREDICTION_NOT_A_MEASUREMENT";
    return out;
}

void TargetTracker::reset() noexcept { impl_->state_.clear(); }
const TrackingConfig &TargetTracker::config() const noexcept { return impl_->config_; }
} // namespace mark::tracking
