#include "vision/tracking/target_tracker.hpp"
#include "filter/kalman_cv.hpp"
#include "vision/math/covariance_validation.hpp"
#include "assertions.hpp"
#include <iostream>
#include <limits>
using namespace mark::tracking;
TrackingFrame frame(uint64_t id, int64_t time, double x, bool missing = false)
{
    TrackingFrame f;
    auto &s = f.stamp_;
    s.session_id_ = "test";
    s.camera_id_ = "test-camera";
    s.clock_domain_ = "nominal";
    s.pixel_domain_ = mark::pose::PixelDomain::RawDistorted;
    s.frame_id_ = id;
    s.received_us_ = time;
    if (!missing)
    {
        mark::pose::PoseObservation p;
        p.stamp_ = s;
        p.source_ = "MARK";
        p.target_id_ = "mark-white-structure-v1";
        p.estimate_.emplace();
        p.estimate_->pose_.t_m_ = {x, 0, 2};
        p.estimate_->rmse_px_ = 50;
        p.estimate_->translation_spread_m2_(2, 2) = .5 * .01 * .01;
        f.observations_.push_back(p);
    }
    return f;
}

int main()
{
    try{
        using test_support::check;
        TrackingConfig c;
        c.profile_ = TrackingProfile{cv::Matx33d::eye() * 1e-4, .1, 4};
        TargetTracker tracker(c);
        auto a = tracker.step(frame(0, 0, 0));
        check(a.input_accepted_ && a.tracks_.size() == 1 && !a.tracks_[0].velocity_mps_, "first estimate rejected or fake velocity");
        check(std::abs(a.tracks_[0].covariance_(2, 2) - .00015) < 1e-12, "G2 position spread not consumed by G4 noise");
        auto b = tracker.step(frame(1, 100000, .01));
        check(b.tracks_[0].velocity_mps_ && cv::norm(*b.tracks_[0].velocity_mps_ - cv::Vec3d(.1, 0, 0)) < 1e-10, "two sample velocity");
        auto jump = tracker.step(frame(2, 200000, 10));
        check(jump.tracks_[0].state_ == TrackState::Measured && jump.association_nis_ && *jump.association_nis_ > 12.6, "NIS gate still active");
        auto long_dt = tracker.step(frame(3, 3200000, 11));
        check(long_dt.tracks_[0].state_ == TrackState::Measured && long_dt.tracks_[0].velocity_mps_, "max_dt/std gate still active");
        auto coast = tracker.step(frame(4, 6200000, 0, true));
        check(coast.tracks_[0].state_ == TrackState::Coasting && cv::checkRange(cv::Mat(coast.tracks_[0].position_m_)) && coast.tracks_[0].velocity_mps_, "variance gate erased finite coast");
        check(mark::observed::positive_definite(cv::Mat(coast.tracks_[0].covariance_)), "P invalid");
        check(!tracker.step(frame(4, 6200001, 0)).input_accepted_, "duplicate accepted");
        auto invalid = frame(5, 6300000, 0);
        invalid.observations_[0].estimate_->pose_.t_m_[0] = std::numeric_limits<double>::quiet_NaN();
        auto n = tracker.step(invalid);
        check(n.tracks_[0].state_ == TrackState::Coasting, "bad measurement poisoned state");
        check(tracker.step(frame(6, 6400000, 12)).tracks_[0].state_ == TrackState::Measured, "recovery failed");
        bool failed = false;
        try{
            detail::CvFilter f;
            f.bootstrap({0, 0, 2}, cv::Matx33d::eye(), {0, 0, 2}, cv::Matx33d::eye(), 0, .1);
        }
        catch (const std::exception &){
            failed = true;
        }
        check(failed, "dt zero bootstrap");
        detail::CvFilter bounded;
        bounded.initialize({0, 0, 2}, cv::Matx33d::eye() * 1e-4, 4);
        bounded.predict(.1, .1, .001);
        check(mark::observed::positive_definite(cv::Mat(bounded.covariance())), "bounded predict P");
        failed = false;
        try{
            bounded.bootstrap({0, 0, 2}, cv::Matx33d::eye(), {0, 0, 2}, cv::Matx33d::eye(), .1, .1, .1);
        }
        catch (const std::exception &){
            failed = true;
        }
        check(failed, "overlap division bound accepted");
        // G4只需要G2的位置和分歧矩阵；未使用的RMSE不再是隐含准入门。
        TargetTracker zero_spread(c);
        auto single = frame(0, 0, 0);
        single.observations_[0].estimate_->translation_spread_m2_ = cv::Matx33d::zeros();
        single.observations_[0].estimate_->rmse_px_ = std::numeric_limits<double>::quiet_NaN();
        auto unspread = zero_spread.step(single);
        check(unspread.tracks_.size() == 1 && std::abs(unspread.tracks_[0].covariance_(2, 2) - .0001) < 1e-12,
              "single estimate requires fabricated alternatives or unused RMSE gate survived");
        auto bad_spread = frame(1, 100000, 0);
        bad_spread.observations_[0].estimate_->translation_spread_m2_(0, 0) = std::numeric_limits<double>::quiet_NaN();
        auto ignored = zero_spread.step(bad_spread);
        check(ignored.tracks_[0].state_ == TrackState::Coasting, "nonfinite consumed spread accepted");
        auto zero_q = c;
        zero_q.profile_->acceleration_psd_m2ps3_ = 0;
        TargetTracker deterministic(zero_q);
        check(deterministic.step(frame(0, 0, 0)).tracks_.size() == 1, "q=0 config wrongly rejected");
        auto changed = frame(7, 6500000, 1);
        changed.stamp_.camera_id_ = "different-camera";
        changed.observations_[0].stamp_ = changed.stamp_;
        auto fresh = tracker.step(changed);
        check(fresh.tracks_.size() == 1 && fresh.tracks_[0].track_id_ != a.tracks_[0].track_id_ && !fresh.tracks_[0].velocity_mps_, "context carried old velocity");
        auto bad_stamp = changed;
        bad_stamp.stamp_.frame_id_ = 8;
        bad_stamp.stamp_.exposure_us_ = bad_stamp.stamp_.received_us_ + 1;
        check(!tracker.step(bad_stamp).input_accepted_, "impossible exposure stamp accepted");
        auto negative = c;
        negative.profile_->camera_mse_m2_(0, 0) = -1;
        failed = false;
        try{
            TargetTracker t(negative);
        }
        catch (const std::exception &){
            failed = true;
        }
        check(failed, "negative R accepted");
        return 0;
    }
    catch (const std::exception &e){
        std::cerr << e.what() << '\n';
        return 1;
    }
}
