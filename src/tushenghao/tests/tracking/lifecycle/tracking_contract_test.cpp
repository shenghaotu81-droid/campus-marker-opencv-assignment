// G4 主动契约测试：所有数值均为精确/理想 fixture 条件，绝不是生产阈值建议。
// 原 Release 下 assert 会消失；本文件用抛异常的主动检查，失败必须返回非零。
#include "vision/tracking/target_tracker.hpp"
#include "filter/kalman_cv.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace mark::tracking;
using mark::pose::PoseStatus;

void require(bool value, const std::string &message)
{
    if (!value)
        throw std::runtime_error(message);
}

void near(double actual, double expected, double epsilon, const std::string &message)
{
    require(std::isfinite(actual) && std::abs(actual - expected) <= epsilon, message);
}

// 理想场景的手算参数用于激发分支；生产参数必须由独立标定工具给出。
TrackingConfig fixture_config()
{
    TrackingConfig config;
    TrackingProfile profile;
    profile.camera_mse_m2_ = cv::Matx33d::eye() * 1e-4;
    profile.camera_id_ = "contract-camera";
    profile.acceleration_psd_m2ps3_ = 0.1;
    profile.initial_velocity_variance_m2ps2_ = 4.0;
    profile.association_nis_limit_ = 9.0;
    profile.bootstrap_nis_limit_ = 9.0;
    profile.position_std_limit_m_ = 10.0;
    profile.velocity_std_limit_mps_ = 10.0;
    profile.max_dt_s_ = 2.0;
    profile.evidence_id_ = "ideal-contract-fixture-not-production";
    profile.basis_ = "synthetic";
    config.profile_ = profile;
    return config;
}

// 曝光和接收显式分开；Synthetic 只代表已知测试时间轴，不能获得实车资格。
TrackingFrame empty_frame(std::uint64_t sequence, double seconds)
{
    TrackingFrame frame;
    frame.origin_ = InputOrigin::Synthetic;
    frame.stamp_.session_id_ = "contract-session";
    frame.stamp_.camera_id_ = "contract-camera";
    frame.stamp_.clock_domain_ = "contract-monotonic";
    frame.stamp_.pixel_domain_ = mark::pose::PixelDomain::RawDistorted;
    frame.stamp_.frame_id_ = sequence;
    const auto time = static_cast<std::int64_t>(1000000 + std::llround(seconds * 1e6));
    frame.stamp_.exposure_us_ = time;
    frame.stamp_.received_us_ = time + 1000;
    frame.stamp_.time_uncertainty_s_ = 0;
    return frame;
}

// 模拟 G2 真正发布的 Unique：选中索引、候选证据、来源时间与最终位姿缺一不可。
mark::pose::PoseObservation measurement(const TrackingFrame &frame, cv::Vec3d position,
                                      std::size_t index = 0)
{
    mark::pose::PoseObservation observation;
    observation.stamp_ = frame.stamp_;
    observation.source_ = "MARK";
    observation.target_id_ = "mark-white-structure-v1";
    observation.detection_index_ = index;
    observation.status_ = PoseStatus::Unique;
    mark::pose::PoseCandidate candidate;
    candidate.pose_.t_m_ = position;
    candidate.rmse_px_ = 0;
    candidate.max_error_px_ = 0;
    candidate.min_depth_m_ = position[2];
    candidate.metrics_valid_ = true;
    candidate.eligible_ = true;
    observation.candidates_.push_back(candidate);
    observation.selected_index_ = 0;
    observation.T_camera_target_ = candidate.pose_;
    observation.selected_rmse_px_ = 0;
    return observation;
}

TrackingFrame observed_frame(std::uint64_t sequence, double seconds, cv::Vec3d position)
{
    auto frame = empty_frame(sequence, seconds);
    frame.observations_.push_back(measurement(frame, position));
    return frame;
}

// 测试读取快照副本，避免 step 临时结果析构后留下悬空引用。
TargetTrack one_track(const TrackingResult &result, const std::string &where)
{
    require(result.input_accepted_ && result.ready_ && result.tracks_.size() == 1,
            where + ": 必须返回一条有效生命周期记录");
    return result.tracks_.front();
}

void establish(TargetTracker &tracker)
{
    tracker.step(observed_frame(1, 0.0, {0, 0, 2}));
    const auto result = tracker.step(observed_frame(2, 0.1, {0.05, 0, 2}));
    const auto &track = one_track(result, "建立两点轨迹");
    require(track.state_ == TrackState::Measured && track.confirmed_ && track.velocity_mps_,
            "两次独立测量后必须确认速度");
}

// F/Q 与两点初始化的交叉协方差用手算值核验，不让“输出平滑”掩盖矩阵错误。
void test_filter_math()
{
    detail::CvFilter filter;
    const cv::Matx33d noise(0.04, 0, 0, 0, 0.09, 0, 0, 0, 0.16);
    filter.initialize({1, 2, 3}, noise, 2.0);
    filter.predict(0.2, 0.6);
    const auto predicted = filter.covariance();
    for (int axis = 0; axis < 3; ++axis)
    {
        near(predicted(axis, axis), noise(axis, axis) + 2.0 * 0.04 + 0.6 * 0.008 / 3,
             1e-12, "M01 实际 dt 的 Qpp/F 必须正确");
        near(predicted(axis, axis + 3), 2.0 * 0.2 + 0.6 * 0.04 / 2,
             1e-12, "M02 实际 dt 的 Qpv 必须正确");
        near(predicted(axis + 3, axis + 3), 2.0 + 0.6 * 0.2,
             1e-12, "M03 实际 dt 的 Qvv 必须正确");
    }
    const auto p = cv::Mat(predicted).clone();
    cv::Mat h = cv::Mat::zeros(3, 6, CV_64F);
    const cv::Mat identity = cv::Mat::eye(3, 3, CV_64F);
    identity.copyTo(h.colRange(0, 3));
    const cv::Mat r(noise);
    const cv::Mat s = h * p * h.t() + r;
    const cv::Mat k = p * h.t() * s.inv();
    const cv::Mat a = cv::Mat::eye(6, 6, CV_64F) - k * h;
    const cv::Mat expected = a * p * a.t() + k * r * k.t();
    const cv::Vec3d residual = cv::Vec3d(1.1,2.2,2.9)-filter.position();
    const cv::Mat independent_score = cv::Mat(residual).t()*s.inv()*cv::Mat(residual);
    const auto innovation = filter.innovation({1.1,2.2,2.9},noise);
    near(innovation.score,independent_score.at<double>(0),1e-12,"innovation score differs from independent solve");
    auto other = filter;
    bool wrong_owner=false;
    try { other.correct(innovation); }
    catch(const std::runtime_error &error) { wrong_owner=std::string(error.what())=="TRACK_STALE_INNOVATION"; }
    require(wrong_owner,"innovation reused across filter instances");
    filter.correct(innovation);
    bool stale=false;
    try { filter.correct(innovation); }
    catch(const std::runtime_error &error) { stale=std::string(error.what())=="TRACK_STALE_INNOVATION"; }
    require(stale,"innovation reused after state update");
    require(cv::norm(cv::Mat(filter.covariance()), expected, cv::NORM_INF) < 1e-12,
            "M04 Joseph 更新必须符合完整公式");
    for (int i = 0; i < 80; ++i)
    {
        filter.predict(0.01 + (i % 4) * 0.003, 0.6);
        filter.correct({1.1, 2.2, 2.9}, noise);
        const auto covariance = cv::Mat(filter.covariance());
        require(cv::checkRange(covariance) &&
                cv::norm(covariance, covariance.t(), cv::NORM_INF) < 1e-10,
                "M05 重复预测/更新的协方差必须有限、对称");
        cv::Mat eigenvalues;
        require(cv::eigen(covariance, eigenvalues) && eigenvalues.at<double>(5) >= -1e-12,
                "M06 Joseph 更新必须保持半正定");
    }
    detail::CvFilter bootstrap;
    const cv::Matx33d previous = cv::Matx33d::eye() * 0.01;
    const cv::Matx33d current = cv::Matx33d::eye() * 0.04;
    bootstrap.bootstrap({1, 2, 3}, previous, {1.2, 2.4, 2.8}, current, 0.2, 0);
    near(bootstrap.velocity()[0], 1, 1e-12, "M07 两点速度必须除实际 dt");
    near(bootstrap.velocity()[1], 2, 1e-12, "M08 y 速度符号/单位必须正确");
    near(bootstrap.velocity()[2], -1, 1e-12, "M09 z 速度符号/单位必须正确");
    for (int axis = 0; axis < 3; ++axis)
    {
        near(bootstrap.covariance()(axis, axis), 0.04, 1e-12,
             "M10 两点初始化位置协方差来自当前样本");
        near(bootstrap.covariance()(axis, axis + 3), 0.04 / 0.2, 1e-12,
             "M11 位置与速度使用同一当前样本，交叉协方差不能漏掉或写负");
        near(bootstrap.covariance()(axis + 3, axis + 3), 0.05 / 0.04, 1e-12,
             "M12 两点速度方差必须传播两次位置误差");
    }
}

void test_readiness_and_bootstrap()
{
    TrackingConfig empty;
    TargetTracker not_ready(empty);
    const auto blocked = not_ready.step(observed_frame(1, 0, {0, 0, 2}));
    require(!blocked.ready_ && blocked.tracks_.empty(), "T01 未标定 profile 不能创建轨迹");
    TargetTracker tracker(fixture_config());
    auto result = tracker.step(observed_frame(1, 0, {0, 0, 2}));
    const auto first_id = one_track(result, "T02").track_id_;
    require(result.tracks_[0].state_ == TrackState::Measured &&
            !result.tracks_[0].confirmed_ && !result.tracks_[0].velocity_mps_,
            "T02 单帧只有位置，不能捏造零速度为已估速度");
    result = tracker.step(observed_frame(2, 0.2, {0.2, 0, 2}));
    const auto &track = one_track(result, "T03");
    require(track.track_id_ == first_id && track.confirmed_ && track.velocity_mps_,
            "T03 两帧必须确认同一轨迹");
    near((*track.velocity_mps_)[0], 1, 1e-9,
         "T04 相机相对速度不应被规则0.5m/s限幅");
    require(!track.qualified_for_g5_ && !track.time_bounded_,
            "T05 nominal_research 数值通过不能赋予实车时间/任务资格");
}

// 七态不得压成“有候选就有测量”；所有非 Unique 状态都只会推进缺测生命周期。
void test_pose_statuses_and_evidence()
{
    const std::vector<PoseStatus> missing{
        PoseStatus::InvalidInput, PoseStatus::CorrespondenceMissing,
        PoseStatus::NotDetected, PoseStatus::NoSolution,
        PoseStatus::BudgetMissing, PoseStatus::Ambiguous};
    for (const auto status : missing)
    {
        TargetTracker tracker(fixture_config());
        establish(tracker);
        auto frame = observed_frame(3, 0.2, {0.1, 0, 2});
        frame.observations_[0].status_ = status;
        // 刻意留下貌似完整的 T 和候选，证明状态本身不能被绕过。
        const auto result = tracker.step(frame);
        require(one_track(result, "T06").state_ == TrackState::Coasting,
                "T06 非 Unique 不得更新为实测");
        require(result.tracks_[0].last_measurement_stamp_.frame_id_ == 2,
                "T07 非 Unique 不得刷新最近实测时间");
    }
    for (int defect = 0; defect < 6; ++defect)
    {
        TargetTracker tracker(fixture_config());
        establish(tracker);
        auto frame = observed_frame(3, 0.2, {0.1, 0, 2});
        auto &observation = frame.observations_[0];
        if (defect == 0) observation.selected_index_.reset();
        if (defect == 1) observation.T_camera_target_.reset();
        if (defect == 2) observation.candidates_[0].eligible_ = false;
        if (defect == 3) observation.candidates_[0].metrics_valid_ = false;
        if (defect == 4) observation.candidates_[0].reasons_.push_back("REJECTED_FIXTURE");
        if (defect == 5) ++observation.stamp_.frame_id_;
        require(one_track(tracker.step(frame), "T08").state_ == TrackState::Coasting,
                "T08 Unique 的选中证据/帧绑定缺失不能当测量");
    }
    TargetTracker wrong_source(fixture_config());
    auto frame = observed_frame(1, 0, {0, 0, 2});
    frame.observations_[0].source_ = "APRILTAG";
    require(wrong_source.step(frame).tracks_.empty(), "T09 不混入退出Tag来源");
    frame.observations_[0].source_ = "MARK";
    frame.observations_[0].target_id_ = "another-model";
    ++frame.stamp_.frame_id_;
    frame.observations_[0].stamp_ = frame.stamp_;
    frame.stamp_.received_us_ += 1000;
    frame.stamp_.exposure_us_ = *frame.stamp_.exposure_us_ + 1000;
    frame.observations_[0].stamp_ = frame.stamp_;
    require(wrong_source.step(frame).tracks_.empty(), "T10 不混入其他目标模型");
}

void test_association_and_reseed()
{
    TargetTracker tracker(fixture_config());
    establish(tracker);
    auto frame = observed_frame(3, 0.2, {0.1, 0, 2});
    frame.observations_.push_back(measurement(frame, {0.11, 0, 2}, 1));
    const auto competing = tracker.step(frame);
    const auto old_id = one_track(competing, "T11").track_id_;
    require(competing.tracks_[0].state_ == TrackState::Coasting,
            "T11 多个关联候选都合格必须拒绝，不能挑最近者");
    auto jump = tracker.step(observed_frame(4, 0.3, {20, 0, 2}));
    require(one_track(jump, "T12").track_id_ == old_id &&
            jump.tracks_[0].state_ == TrackState::Coasting,
            "T12 首次门外测量只建立独立pending，不污染旧track");
    auto reseeded = tracker.step(observed_frame(5, 0.4, {20.05, 0, 2}));
    const auto &new_track = one_track(reseeded, "T13");
    require(new_track.track_id_ != old_id && new_track.state_ == TrackState::Measured &&
            new_track.confirmed_ && !reseeded.events_.empty(),
            "T13 独立两点一致后新ID替代旧轨迹，且事件不可丢失");
    // 极小门控故意使反转落在门外；独立pending必须允许恢复，不能永远拒绝机动。
    auto cfg = fixture_config();
    cfg.profile_->association_nis_limit_ = 1e-4;
    cfg.profile_->bootstrap_nis_limit_ = 9;
    TargetTracker reversal(cfg);
    establish(reversal);
    const auto before = reversal.step(observed_frame(3, 0.2, {0.1, 0, 2}));
    const auto before_id = one_track(before, "T14").track_id_;
    reversal.step(observed_frame(4, 0.3, {0.05, 0, 2}));
    const auto after = reversal.step(observed_frame(5, 0.4, {0, 0, 2}));
    const auto &after_track = one_track(after, "T14");
    require(after_track.state_ == TrackState::Measured && after_track.confirmed_ &&
            after_track.track_id_ != before_id && after_track.velocity_mps_ &&
            (*after_track.velocity_mps_)[0] < 0,
            "T14 随机反转须可由独立两点重建恢复，不允许永久被门控拒绝");
}

void test_time_order_and_source_changes()
{
    TargetTracker tracker(fixture_config());
    TargetTracker reference(fixture_config());
    establish(tracker);
    establish(reference);
    auto duplicate = observed_frame(2, 0.1, {999, 0, 2});
    require(!tracker.step(duplicate).input_accepted_, "T15 重复帧必须拒绝");
    auto backward = observed_frame(3, 0.05, {999, 0, 2});
    require(!tracker.step(backward).input_accepted_, "T16 倒序时间必须拒绝");
    auto zero_dt = observed_frame(3, 0.1, {999, 0, 2});
    require(!tracker.step(zero_dt).input_accepted_, "T17 零dt必须拒绝，不能除零或造速度");
    const auto valid = tracker.step(observed_frame(3, 0.2, {0.1, 0, 2}));
    const auto clean = reference.step(observed_frame(3, 0.2, {0.1, 0, 2}));
    require(valid.tracks_[0].track_id_ == clean.tracks_[0].track_id_ &&
            cv::norm(*valid.tracks_[0].position_m_ - *clean.tracks_[0].position_m_) < 1e-12 &&
            cv::norm(cv::Mat(*valid.tracks_[0].covariance_),
                     cv::Mat(*clean.tracks_[0].covariance_), cv::NORM_INF) < 1e-12,
            "T18 拒绝帧不能污染下一个合法结果");
    const auto old_id = valid.tracks_[0].track_id_;
    auto changed = observed_frame(1, 0, {0, 0, 2});
    changed.stamp_.session_id_ = "new-session";
    changed.observations_[0].stamp_ = changed.stamp_;
    const auto reset = tracker.step(changed);
    const auto &fresh = one_track(reset, "T19");
    require(fresh.track_id_ != old_id && !fresh.confirmed_ && !fresh.velocity_mps_,
            "T19 新会话可重启计时，但不能复用旧速度或ID");
    for (int change = 0; change < 2; ++change)
    {
        TargetTracker separate(fixture_config());
        establish(separate);
        auto old = separate.predict_to(1100000);
        auto frame = observed_frame(3, 0.2, {0.1, 0, 2});
        if (change == 0) frame.stamp_.camera_id_ = "another-camera";
        if (change == 1) frame.stamp_.clock_domain_ = "another-clock";
        frame.observations_[0].stamp_ = frame.stamp_;
        const auto result = separate.step(frame);
        require(one_track(result, "T20").track_id_ != old.track_id_ &&
                !result.tracks_[0].confirmed_, "T20 相机/时钟变更必须断旧状态");
    }
}

void test_bounded_time()
{
    auto cfg = fixture_config();
    cfg.time_policy_ = "bounded_only";
    cfg.stamp_uncertainty_semantics_ = "absolute_bound";
    cfg.max_time_uncertainty_s_ = 0.003;
    cfg.profile_->validated_time_bound_s_ = 0.003;
    TargetTracker tracker(cfg);
    auto first = observed_frame(1, 0, {0, 0, 2});
    first.stamp_.time_uncertainty_s_ = 0.001;
    first.observations_[0].stamp_ = first.stamp_;
    const auto initial = tracker.step(first);
    require(one_track(initial, "T21").time_bounded_, "T21 已知绝对时间界可进入有界模型");
    auto unknown = observed_frame(2, 0.1, {0.05, 0, 2});
    unknown.stamp_.time_uncertainty_s_.reset();
    unknown.observations_[0].stamp_ = unknown.stamp_;
    const auto coast = tracker.step(unknown);
    require(one_track(coast, "T22").state_ == TrackState::Coasting &&
            coast.tracks_[0].last_measurement_stamp_.frame_id_ == 1,
            "T22 有界模式缺时间界时只能缺测，不可用0代替");
    auto excessive = observed_frame(3, 0.2, {0.1, 0, 2});
    excessive.stamp_.time_uncertainty_s_ = 0.02;
    excessive.observations_[0].stamp_ = excessive.stamp_;
    require(one_track(tracker.step(excessive), "T23").state_ == TrackState::Coasting,
            "T23 超过已验证时间域不能更新");
    auto recovered = observed_frame(4, 0.3, {0.15, 0, 2});
    recovered.stamp_.time_uncertainty_s_ = 0.001;
    recovered.observations_[0].stamp_ = recovered.stamp_;
    require(one_track(tracker.step(recovered), "T24").state_ == TrackState::Measured,
            "T24 合法时间重新出现后可恢复测量");
}

void test_uncertainty_and_invalid_values()
{
    auto cfg = fixture_config();
    TargetTracker tracker(cfg);
    auto first = observed_frame(1, 0, {0, 0, 2});
    first.observations_[0].covariance_se3_ = std::array<double, 36>{};
    const auto initial = tracker.step(first);
    require(one_track(initial, "T25").covariance_ &&
            (*initial.tracks_[0].covariance_)(0, 0) > 0,
            "T25 G2空/零SE3占位不能冒充零位置误差");
    TargetTracker sidecar(cfg);
    first.position_noise_.push_back({0, cv::Matx33d::eye() * 0.04, "known-fixture-R", false});
    const auto independent = sidecar.step(first);
    near((*one_track(independent, "T26").covariance_)(0, 0), 0.04, 1e-12,
         "T26 独立PositionNoise按唯一检测索引匹配，不能忽略或复用别项");
    auto invalid = observed_frame(2, 0.1, {0.05, 0, 2});
    invalid.position_noise_.push_back({0, cv::Matx33d::eye() * -1, "bad-R", false});
    require(one_track(tracker.step(invalid), "T27").state_ == TrackState::Coasting,
            "T27 负定位置R必须拒绝，不可偷偷改成正数或退回profile");
    auto nan = observed_frame(3, 0.2, {0.1, 0, 2});
    nan.observations_[0].T_camera_target_->t_m_[0] = std::numeric_limits<double>::quiet_NaN();
    require(one_track(tracker.step(nan), "T28").state_ == TrackState::Coasting,
            "T28 NaN不能进入滤波矩阵");
    TargetTracker duplicate_noise(cfg);
    first.position_noise_.push_back({0, cv::Matx33d::eye() * 0.09, "duplicate-index-R", false});
    require(duplicate_noise.step(first).tracks_.empty(),
            "T29 同一检测索引存在两个位置R必须拒绝，不能随便选一个");
}

// 预测只读，LOST不带旧测量，恢复新建ID，三者共同防止旧结果被包装成当前观察。
void test_forecast_and_loss()
{
    TargetTracker tracker(fixture_config());
    TargetTracker reference(fixture_config());
    establish(tracker);
    establish(reference);
    const auto predicted = tracker.predict_to(1150000);
    require(predicted.available_ && predicted.position_m_ && predicted.velocity_mps_,
            "T30 合法短期预测应独立返回");
    const auto a = tracker.step(observed_frame(3, 0.2, {0.1, 0, 2}));
    const auto b = reference.step(observed_frame(3, 0.2, {0.1, 0, 2}));
    require(cv::norm(*a.tracks_[0].position_m_ - *b.tracks_[0].position_m_) < 1e-12 &&
            cv::norm(cv::Mat(*a.tracks_[0].covariance_),
                     cv::Mat(*b.tracks_[0].covariance_), cv::NORM_INF) < 1e-12,
            "T31 predict_to不得推进step历史或改后续滤波");
    auto cfg = fixture_config();
    cfg.profile_->position_std_limit_m_ = 0.03;
    TargetTracker loss(cfg);
    establish(loss);
    const auto old_id = loss.predict_to(1100000).track_id_;
    bool lost = false;
    std::uint64_t next = 3;
    double time = 0.2;
    for (; next < 40; ++next, time += 0.1)
    {
        const auto result = loss.step(empty_frame(next, time));
        const auto &track = one_track(result, "T32");
        if (track.state_ == TrackState::Lost)
        {
            require(!track.position_m_ && !track.velocity_mps_,
                    "T32 LOST不得带旧位置/速度伪装当前测量");
            lost = true;
            break;
        }
    }
    require(lost, "T33 连续缺测的误差增长必须使轨迹LOST");
    const auto reacquired = loss.step(observed_frame(next + 1, time + 0.1, {0.2, 0, 2}));
    require(one_track(reacquired, "T34").track_id_ != old_id &&
            !reacquired.tracks_[0].confirmed_ && !reacquired.tracks_[0].velocity_mps_,
            "T34 LOST后重现新ID，从位置重新初始化速度");
    TargetTracker gap(fixture_config());
    establish(gap);
    const auto long_gap = gap.step(empty_frame(3, 3.0));
    require(one_track(long_gap, "T35").state_ == TrackState::Lost,
            "T35 超过已标定max_dt必须LOST，不能大步外推");
}

// 实际同一静止目标：补偿前相机表观移动，补偿后世界位置/速度保持不动。
void test_world_coordinates()
{
    auto cfg = fixture_config();
    cfg.coordinate_mode_ = CoordinateMode::LocalWorld;
    cfg.world_frame_id_ = "contract-world";
    TargetTracker world(cfg);
    TargetTracker camera(fixture_config());
    TrackingResult last_world, last_camera;
    for (std::uint64_t n = 0; n < 8; ++n)
    {
        auto frame = observed_frame(n + 1, n * 0.1, {1.0 - n * 0.05, 0, 2});
        last_camera = camera.step(frame);
        CameraTransform transform;
        transform.stamp_ = frame.stamp_;
        transform.world_frame_id_ = cfg.world_frame_id_;
        transform.t_world_camera_m_ = {n * 0.05, 0, 0};
        transform.covariance_ = Mat6::eye() * 1e-12;
        frame.camera_transform_ = transform;
        last_world = world.step(frame);
    }
    require(one_track(last_world, "T36").velocity_mps_ &&
            cv::norm(*last_world.tracks_[0].velocity_mps_) < 1e-10,
            "T36 自身平动补偿后静止世界目标速度应为0");
    require(one_track(last_camera, "T37").velocity_mps_ &&
            (*last_camera.tracks_[0].velocity_mps_)[0] < -0.4,
            "T37 相机相对导数必须诚实反映本车运动，不能叫世界速度");
    auto no_transform = observed_frame(9, 0.8, {0.6, 0, 2});
    const auto coast = world.step(no_transform);
    require(one_track(coast, "T38").state_ == TrackState::Coasting &&
            coast.tracks_[0].coordinate_frame_ == last_world.tracks_[0].coordinate_frame_,
            "T38 world缺自身变换只能缺测，不能静默切camera坐标继续滤波");
    TargetTracker rotating(cfg);
    const cv::Vec3d fixed(1, 0.4, 2);
    TrackingResult last;
    for (std::uint64_t n = 0; n < 8; ++n)
    {
        const double angle = n * 0.03;
        const cv::Matx33d rotation(std::cos(angle), -std::sin(angle), 0,
                                  std::sin(angle), std::cos(angle), 0, 0, 0, 1);
        const cv::Vec3d translation(n * 0.02, 0, 0);
        auto frame = observed_frame(n + 1, n * 0.1, rotation.t() * (fixed - translation));
        CameraTransform transform;
        transform.stamp_ = frame.stamp_;
        transform.world_frame_id_ = cfg.world_frame_id_;
        transform.R_world_camera_ = rotation;
        transform.t_world_camera_m_ = translation;
        transform.covariance_ = Mat6::eye() * 1e-12;
        frame.camera_transform_ = transform;
        last = rotating.step(frame);
    }
    require(one_track(last, "T39").position_m_ && last.tracks_[0].velocity_mps_ &&
            cv::norm(*last.tracks_[0].position_m_ - fixed) < 1e-10 &&
            cv::norm(*last.tracks_[0].velocity_mps_) < 1e-10,
            "T39 相机转动和平动同时补偿，不能只相加底盘速度");
}

// 非法像素域是坏输入；拒绝不能偷偷清旧历史，再让下一合法帧失去连续性。
void test_context_edge_cases()
{
    TargetTracker tracker(fixture_config());
    TargetTracker reference(fixture_config());
    establish(tracker);
    establish(reference);
    auto invalid = observed_frame(3, 0.2, {0.1, 0, 2});
    invalid.stamp_.pixel_domain_ = mark::pose::PixelDomain::Unknown;
    invalid.observations_[0].stamp_ = invalid.stamp_;
    require(!tracker.step(invalid).input_accepted_, "T40 Unknown像素域必须拒绝");
    const auto actual = tracker.step(observed_frame(3, 0.2, {0.1, 0, 2}));
    const auto expected = reference.step(observed_frame(3, 0.2, {0.1, 0, 2}));
    require(one_track(actual, "T40").track_id_ == one_track(expected, "T40").track_id_ &&
            cv::norm(*actual.tracks_[0].position_m_ - *expected.tracks_[0].position_m_) < 1e-12 &&
            cv::norm(cv::Mat(*actual.tracks_[0].covariance_),
                     cv::Mat(*expected.tracks_[0].covariance_), cv::NORM_INF) < 1e-12,
            "T40 非法像素域不得污染下一个合法结果");
    const auto old_id = actual.tracks_[0].track_id_;
    auto receive_time = observed_frame(4, 0.3, {0.15, 0, 2});
    receive_time.stamp_.exposure_us_.reset();
    receive_time.observations_[0].stamp_ = receive_time.stamp_;
    auto changed = tracker.step(receive_time);
    require(one_track(changed, "T41").track_id_ != old_id &&
            !changed.tracks_[0].confirmed_ && !changed.tracks_[0].velocity_mps_,
            "T41 exposure切receive必须重建，不能混入旧时间源速度");
    const auto received_id = changed.tracks_[0].track_id_;
    changed = tracker.step(observed_frame(5, 0.4, {0.2, 0, 2}));
    require(one_track(changed, "T41").track_id_ != received_id && !changed.tracks_[0].confirmed_,
            "T41 receive切回exposure也必须重建");

    auto world_config = fixture_config();
    world_config.coordinate_mode_ = CoordinateMode::LocalWorld;
    world_config.world_frame_id_ = "fixed-world-frame";
    TargetTracker world(world_config);
    TrackingResult last;
    for (std::uint64_t n = 1; n <= 2; ++n)
    {
        auto frame = observed_frame(n, (n - 1) * 0.1, {0, 0, 2});
        CameraTransform transform;
        transform.stamp_ = frame.stamp_;
        transform.world_frame_id_ = world_config.world_frame_id_;
        transform.covariance_ = Mat6::eye() * 1e-12;
        frame.camera_transform_ = transform;
        last = world.step(frame);
    }
    const auto world_id = one_track(last, "T42").track_id_;
    auto wrong_world = observed_frame(3, 0.2, {5, 0, 2});
    CameraTransform changed_transform;
    changed_transform.stamp_ = wrong_world.stamp_;
    changed_transform.world_frame_id_ = "a-different-world";
    changed_transform.covariance_ = Mat6::eye() * 1e-12;
    wrong_world.camera_transform_ = changed_transform;
    const auto refused = world.step(wrong_world);
    require(one_track(refused, "T42").state_ == TrackState::Coasting &&
            refused.tracks_[0].track_id_ == world_id &&
            refused.tracks_[0].coordinate_frame_ == "fixed-world-frame",
            "T42 外部world_frame不匹配必须原坐标coast，不能偷偷切坐标或接收该测量");
}

// 输入采样最大间隔和未来查询跨度是两种量；预测是否可用由误差预算决定。
void test_forecast_horizon_and_time_envelopes()
{
    auto cfg = fixture_config();
    cfg.profile_->max_dt_s_ = 0.11;
    TargetTracker tracker(cfg);
    establish(tracker);
    const auto forecast = tracker.predict_to(1400000);
    require(forecast.available_ && forecast.state_time_us_ == 1400000 && forecast.position_m_,
            "T43 forecast跨度大于max_frame_dt仍可按协方差预算预测，不能混用采样间隔门槛");
    near((*forecast.position_m_)[0], 0.2, 1e-10, "T44 未来查询位置必须使用真实查询dt");

    const auto r = cv::Matx33d::eye() * 0.01;
    detail::CvFilter exact, bounded;
    exact.initialize({1, 2, 3}, r, 2);
    bounded.initialize({1, 2, 3}, r, 2);
    exact.predict(0.2, 0.6, 0);
    bounded.predict(0.2, 0.6, 0.02);
    cv::Mat eigenvalues;
    require(cv::eigen(cv::Mat(bounded.covariance() - exact.covariance()), eigenvalues) &&
            eigenvalues.at<double>(5) >= -1e-12,
            "T45 有界时偏的预测MSE包络不得小于精确时间模型");
    exact.bootstrap({0, 0, 2}, r, {0.2, 0.1, 2}, r, 0.2, 0.6, 0);
    bounded.bootstrap({0, 0, 2}, r, {0.2, 0.1, 2}, r, 0.2, 0.6, 0.02, 0.01);
    require(cv::eigen(cv::Mat(bounded.covariance() - exact.covariance()), eigenvalues) &&
            eigenvalues.at<double>(5) >= -1e-12,
            "T45 有界两点初始化不得产生更小的无依据不确定度");
    // R2回归：零过程噪声时可直接复核位置补项，profile单帧界与区间界分别传入。
    detail::CvFilter position_bound_test;
    position_bound_test.bootstrap({0, 0, 2}, r, {0.2, 0.1, 2}, r, 0.2, 0, 0.02, 0.01);
    const double velocity_mse_x = 4 * 0.02 / (0.18 * 0.18) +
        2 * 0.02 * 0.02 / (0.18 * 0.18);
    near(position_bound_test.covariance()(0, 0),
         2 * (0.01 + 0.01 * 0.01 * (1 + velocity_mse_x)), 1e-12,
         "R2 profile界平方乘bootstrap速度二阶矩必须进入Ppp");
    bool refused_predict = false, refused_bootstrap = false;
    try { bounded.predict(0.1, 0.6, 0.1); }
    catch (const std::exception &) { refused_predict = true; }
    try { bounded.bootstrap({0, 0, 2}, r, {0.1, 0, 2}, r, 0.1, 0.6, 0.1); }
    catch (const std::exception &) { refused_bootstrap = true; }
    require(refused_predict && refused_bootstrap,
            "T46 B>=dt时可能真实时间不正，预测/两点速度必须拒绝而非继续除法");
}

// 明确构造“当前硬件声明齐全但历史pending来自合成”的混源场景，检查资格不能被洗白。
void test_pending_provenance_and_lost_age()
{
    auto cfg = fixture_config();
    cfg.time_policy_ = "bounded_only";
    cfg.stamp_uncertainty_semantics_ = "absolute_bound";
    cfg.max_time_uncertainty_s_ = 0.001;
    cfg.profile_->time_policy_ = "bounded_only";
    cfg.profile_->validated_time_bound_s_ = 0.001;
    cfg.profile_->hardware_verified_ = true;
    TargetTracker tracker(cfg);
    auto verified_frame = [](std::uint64_t n, double seconds, cv::Vec3d p)
    {
        auto frame = observed_frame(n, seconds, p);
        frame.origin_ = InputOrigin::TypedMemory;
        frame.observations_[0].control_usable_ = true;
        return frame;
    };
    tracker.step(verified_frame(1, 0, {0, 0, 2}));
    const auto trusted = tracker.step(verified_frame(2, 0.1, {0.05, 0, 2}));
    require(one_track(trusted, "T47").qualified_for_g5_,
            "T47 fixture前置条件必须实际激活资格路径，不能以始终false空验");
    auto synthetic_pending = verified_frame(3, 0.2, {20, 0, 2});
    synthetic_pending.origin_ = InputOrigin::Synthetic;
    tracker.step(synthetic_pending);
    const auto mixed = tracker.step(verified_frame(4, 0.3, {20.05, 0, 2}));
    require(one_track(mixed, "T47").track_id_ != trusted.tracks_[0].track_id_ &&
            mixed.tracks_[0].confirmed_ && !mixed.tracks_[0].model_hardware_verified_ &&
            !mixed.tracks_[0].qualified_for_g5_,
            "T47 合成pending即使被后续TypedMemory确认也不得获得硬件/下游资格");

    TargetTracker invalid_candidate(fixture_config());
    establish(invalid_candidate);
    auto invalid = observed_frame(3, 0.2, {0.1, 0, 2});
    invalid.observations_[0].candidates_[0].pose_.rvec_[0] =
        std::numeric_limits<double>::quiet_NaN();
    require(one_track(invalid_candidate.step(invalid), "T48").state_ == TrackState::Coasting,
            "T48 候选pose含NaN而最终T正常也是矛盾证据，不能只检查最终T");

    auto loss_config = fixture_config();
    loss_config.profile_->max_dt_s_ = 0.15;
    TargetTracker loss(loss_config);
    establish(loss);
    const auto first_lost = loss.step(empty_frame(3, 0.4));
    require(one_track(first_lost, "T49").state_ == TrackState::Lost &&
            first_lost.tracks_[0].observation_age_s_, "T49 必须先形成带年龄的LOST记录");
    const auto later_lost = loss.step(empty_frame(4, 0.5));
    require(one_track(later_lost, "T49").state_ == TrackState::Lost &&
            later_lost.tracks_[0].observation_age_s_ &&
            *later_lost.tracks_[0].observation_age_s_ > *first_lost.tracks_[0].observation_age_s_,
            "T49 LOST年龄必须随每帧增长，不能冻结在首次丢失时刻");
    near(*later_lost.tracks_[0].observation_age_s_ - *first_lost.tracks_[0].observation_age_s_,
         0.1, 1e-12, "T49 LOST年龄增长必须与当前帧时刻一致");
}

// 初始化测量噪声略大于“预测沿用域”是合法情形：应允许当前观测连续收敛，不能永远删轨迹。
void test_measured_convergence_vs_coast_budget()
{
    auto cfg = fixture_config();
    // 这只是复现分类错误的精确fixture：R的标准差0.01m，沿用域略低于该值。
    // 禁止将这组数字复制为生产阈值，也不为了测试修改实际标定产物。
    cfg.profile_->position_std_limit_m_ = 0.0095;
    cfg.time_policy_ = "bounded_only";
    cfg.stamp_uncertainty_semantics_ = "absolute_bound";
    cfg.max_time_uncertainty_s_ = 0.001;
    cfg.profile_->time_policy_ = "bounded_only";
    cfg.profile_->validated_time_bound_s_ = 0.001;
    cfg.profile_->hardware_verified_ = true;
    TargetTracker tracker(cfg);
    std::uint64_t id = 0;
    for (std::uint64_t n = 0; n < 12; ++n)
    {
        auto frame = observed_frame(n + 1, n * 0.1, {n * 0.05, 0, 2});
        frame.origin_ = InputOrigin::TypedMemory;
        frame.observations_[0].control_usable_ = true;
        const auto result = tracker.step(frame);
        const auto track = one_track(result, "T50");
        if (n == 0)
            id = track.track_id_;
        require(track.state_ == TrackState::Measured && track.track_id_ == id,
                "T50 每帧有效Unique测量必须持续更新同一ID，不能因初始P稍大而循环LOST/重建");
        if (n == 1)
        {
            require(track.confirmed_ && track.velocity_mps_ && track.time_bounded_ &&
                    track.model_hardware_verified_ && track.covariance_,
                    "T50 二点时其他资格前置必须实际激活，不能用hw=false空验");
            require(std::sqrt((*track.covariance_)(0, 0)) > cfg.profile_->position_std_limit_m_ &&
                    !track.qualified_for_g5_,
                    "T50 有当前测量和速度但位置精度未达域时应仅禁止下游资格，不能删除测量");
        }
    }
    const auto missing = tracker.step(empty_frame(13, 1.2));
    const auto lost = one_track(missing, "T50");
    require(lost.state_ == TrackState::Lost && lost.track_id_ == id &&
            !lost.position_m_ && !lost.velocity_mps_ && !lost.qualified_for_g5_,
            "T50 真正缺测进入预测后仍必须按同一位置误差预算LOST，不能一概关闭门控");
}

// 相机标识改变既要重建轨迹，也要重验噪声来源；旧相机的硬件标定不能随新ID自动借用。
void test_profile_camera_provenance()
{
    auto cfg = fixture_config();
    cfg.time_policy_ = "bounded_only";
    cfg.stamp_uncertainty_semantics_ = "absolute_bound";
    cfg.max_time_uncertainty_s_ = 0.001;
    cfg.profile_->time_policy_ = "bounded_only";
    cfg.profile_->validated_time_bound_s_ = 0.001;
    cfg.profile_->hardware_verified_ = true;
    TargetTracker tracker(cfg);
    auto typed_frame = [](std::uint64_t n, double seconds, cv::Vec3d p,
                          const std::string &camera)
    {
        auto frame = observed_frame(n, seconds, p);
        frame.origin_ = InputOrigin::TypedMemory;
        frame.stamp_.camera_id_ = camera;
        frame.observations_[0].stamp_ = frame.stamp_;
        frame.observations_[0].control_usable_ = true;
        return frame;
    };
    tracker.step(typed_frame(1, 0, {0, 0, 2}, "contract-camera"));
    const auto original = tracker.step(typed_frame(2, 0.1, {0.05, 0, 2}, "contract-camera"));
    const auto old = one_track(original, "T51");
    require(old.confirmed_ && old.velocity_mps_ && old.time_bounded_ &&
            old.model_hardware_verified_ && old.qualified_for_g5_,
            "T51 旧相机两样本必须确实激活硬件/时间/速度资格，不能以始终false空验");
    const auto first_new = tracker.step(typed_frame(3, 0.2, {0.1, 0, 2}, "replacement-camera"));
    const auto fresh = one_track(first_new, "T51");
    require(fresh.track_id_ != old.track_id_ && !fresh.confirmed_,
            "T51 换相机必须新ID并从位置重新估速度");
    const auto second_new = tracker.step(typed_frame(4, 0.3, {0.15, 0, 2}, "replacement-camera"));
    const auto recovered = one_track(second_new, "T51");
    require(recovered.track_id_ == fresh.track_id_ && recovered.state_ == TrackState::Measured &&
            recovered.confirmed_ && recovered.velocity_mps_ && recovered.time_bounded_,
            "T51 新相机两样本后必须恢复数值速度，不能仅以首帧未确认挡住资格");
    require(!recovered.model_hardware_verified_ && !recovered.qualified_for_g5_,
            "T51 相机与profile来源不匹配时，即使数值速度已恢复也禁止借用旧硬件资格");
}
// 审查回归：缺界应在profile验证/构造阶段命名拒绝，不能等重建时抛bad_optional_access。
void test_bounded_profile_requires_time_bound()
{
    auto cfg = fixture_config();
    validate_tracking_profile(*cfg.profile_);
    cfg.time_policy_ = "bounded_only";
    cfg.profile_->time_policy_ = "bounded_only";
    bool validation_rejected = false, construction_rejected = false;
    try { validate_tracking_profile(*cfg.profile_); }
    catch (const std::runtime_error &error)
    {
        validation_rejected = std::string(error.what()) == "G4_PROFILE_BOUNDED_TIME_BOUND_MISSING";
    }
    try { TargetTracker tracker(cfg); }
    catch (const std::runtime_error &error)
    {
        construction_rejected = std::string(error.what()) == "G4_PROFILE_BOUNDED_TIME_BOUND_MISSING";
    }
    require(validation_rejected && construction_rejected,
            "审查回归 bounded_only缺validated_time_bound_s必须在验证和构造时命名拒绝");
    cfg.profile_->validated_time_bound_s_ = 0;
    validate_tracking_profile(*cfg.profile_);
    TargetTracker exact_time(cfg);
    std::cout << "G4 bounded profile missing-bound / explicit-zero PASS\n";
}
} // namespace

int main()
{
    try
    {
        test_filter_math();
        test_readiness_and_bootstrap();
        test_pose_statuses_and_evidence();
        test_association_and_reseed();
        test_time_order_and_source_changes();
        test_bounded_time();
        test_uncertainty_and_invalid_values();
        test_forecast_and_loss();
        test_world_coordinates();
        test_context_edge_cases();
        test_forecast_horizon_and_time_envelopes();
        test_pending_provenance_and_lost_age();
        test_measured_convergence_vs_coast_budget();
        test_profile_camera_provenance();
        test_bounded_profile_requires_time_bound();
        std::cout << "G4 tracking contract M01-M12 / T01-T51 PASS\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "G4 tracking contract FAIL: " << error.what() << '\n';
        return 1;
    }
}
