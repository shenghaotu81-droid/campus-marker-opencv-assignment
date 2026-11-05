#include "vision/tracking/target_tracker.hpp"
#include "io/tracking_io.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool value, const char *reason)
{
    if (!value) throw std::runtime_error(reason);
}

// Q04/V06：资源根由构建目标显式提供，测试位置和工作目录均不承担输入契约。
mark::tracking::TrackingFrame frame()
{
    mark::tracking::TrackingFrame f;
    f.origin_ = mark::tracking::InputOrigin::Synthetic;
    f.stamp_.session_id_ = "io-session";
    f.stamp_.camera_id_ = "io-camera";
    f.stamp_.clock_domain_ = "io-clock";
    f.stamp_.pixel_domain_ = mark::pose::PixelDomain::RawDistorted;
    f.stamp_.frame_id_ = 5000000000ULL;
    f.stamp_.received_us_ = 1000000000100LL;
    f.stamp_.exposure_us_ = 1000000000000LL;
    f.stamp_.time_uncertainty_s_ = 0;
    mark::pose::PoseObservation p;
    p.stamp_ = f.stamp_;
    p.source_ = "MARK";
    p.target_id_ = "mark-white-structure-v1";
    p.status_ = mark::pose::PoseStatus::BudgetMissing;
    f.observations_.push_back(p);
    return f;
}
} // namespace

// 主动检查大整数、空帧、旧G2兼容、坏输入与profile往返，不能把空比较当PASS。
int main()
{
    try
    {
        const auto root = std::filesystem::path(VISION_SOURCE_ROOT);
        const auto config = mark::tracking::load_tracking_config(root / "config/tracking.yaml");
        require(!config.profile_, "I01 默认未标定profile应保持空");
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto out = std::filesystem::current_path() / ("g4-io-test-" + std::to_string(nonce));
        require(std::filesystem::create_directory(out), "I02 测试输出必须独占新目录");
        auto a = frame(), b = a;
        ++b.stamp_.frame_id_;
        b.stamp_.received_us_ += 14000;
        *b.stamp_.exposure_us_ += 14000;
        b.observations_.clear();
        mark::tracking::write_tracking_frames({a, b}, out / "native.yaml");
        const auto restored = mark::tracking::read_tracking_frames(out / "native.yaml");
        require(restored.size() == 2 && restored[1].observations_.empty(), "I03 空帧不得丢弃");
        require(restored[0].stamp_.frame_id_ == a.stamp_.frame_id_ &&
            restored[0].stamp_.exposure_us_ == a.stamp_.exposure_us_ &&
            restored[0].stamp_.time_uncertainty_s_ == a.stamp_.time_uncertainty_s_,
            "I04 64位帧号时间及已知零时间误差必须往返");
        require(restored[0].observations_[0].status_ == mark::pose::PoseStatus::BudgetMissing,
            "I05 非Unique不得伪造位姿");
        {
            cv::FileStorage fs((out / "legacy.yaml").string(), cv::FileStorage::WRITE);
            fs << "schema_version" << 1 << "session_id" << "legacy" << "camera_id" << "old-camera"
               << "clock_domain" << "video-index-clock" << "frames" << "["
               << "{" << "frame_id" << "0" << "timestamp_us" << "0" << "mark" << "[" << "]" << "}"
               << "{" << "frame_id" << "1" << "timestamp_us" << "14000" << "mark" << "[" << "]" << "}" << "]";
        }
        const auto legacy = mark::tracking::read_tracking_frames(out / "legacy.yaml");
        require(legacy.size() == 2 && legacy[0].origin_ == mark::tracking::InputOrigin::LegacyG2Replay &&
            !legacy[0].stamp_.exposure_us_ && !legacy[0].stamp_.time_uncertainty_s_,
            "I06 旧回放不能补曝光/时间误差或硬件资格");
        bool rejected = false;
        try { mark::tracking::write_tracking_frames({}, out / "empty.yaml"); }
        catch (const std::exception &) { rejected = true; }
        require(rejected, "I07 空数据集写入必须拒绝");
        {
            std::ofstream file(out / "wrong.yaml");
            file << "%YAML:1.0\n---\nschema_version: 99\nframes: []\n";
        }
        rejected = false;
        try { (void)mark::tracking::read_tracking_frames(out / "wrong.yaml"); }
        catch (const std::exception &) { rejected = true; }
        require(rejected, "I08 错版本必须拒绝");
        mark::tracking::TrackingProfile profile;
        profile.camera_mse_m2_ = cv::Matx33d::eye() * 0.001;
        profile.acceleration_psd_m2ps3_ = 0.1;
        profile.initial_velocity_variance_m2ps2_ = 1;
        profile.association_nis_limit_ = profile.bootstrap_nis_limit_ = 9;
        profile.position_std_limit_m_ = profile.velocity_std_limit_mps_ = 1;
        profile.max_dt_s_ = 1;
        profile.evidence_id_ = "io-fixture-not-production";
        profile.basis_ = "contract_fixture";
        profile.validated_time_bound_s_ = 0;
        mark::tracking::write_tracking_profile(profile, out / "profile.yaml");
        const auto roundtrip = mark::tracking::load_tracking_profile(out / "profile.yaml");
        require(roundtrip.validated_time_bound_s_ && *roundtrip.validated_time_bound_s_ == 0 &&
            roundtrip.evidence_id_ == profile.evidence_id_ &&
            cv::norm(cv::Mat(roundtrip.camera_mse_m2_ - profile.camera_mse_m2_)) == 0,
            "I09 profile数值来源与未验证状态必须往返");
        std::cout << "G4 IO I01-I09 PASS\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
