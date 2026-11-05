#include "io/tracking_io.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace mark::tracking
{
namespace
{
std::string text(const cv::FileNode &n, const char *key)
{
    if (!n[key].isString())
        throw std::runtime_error(std::string("G4_REPLAY_EXPECT_STRING:") + key);
    return static_cast<std::string>(n[key]);
}

// 64位帧号/时间以十进制字符串保存，避免OpenCV FileStorage窄化为32位整数。
std::string integer_token(const cv::FileNode &n)
{
    if (n.isString())
        return static_cast<std::string>(n);
    if (n.isInt())
        return std::to_string(static_cast<int>(n));
    throw std::runtime_error("G4_REPLAY_EXPECT_INTEGER");
}

std::uint64_t unsigned_integer(const cv::FileNode &n)
{
    const auto token = integer_token(n);
    if (token.empty() || token.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("G4_REPLAY_BAD_UNSIGNED_INTEGER");
    return std::stoull(token);
}

std::int64_t signed_integer(const cv::FileNode &n)
{
    const auto token = integer_token(n);
    std::size_t used = 0;
    const auto value = std::stoll(token, &used);
    if (used != token.size())
        throw std::runtime_error("G4_REPLAY_PARTIAL_INTEGER");
    return value;
}

double number(const cv::FileNode &n)
{
    if ((!n.isInt() && !n.isReal()) || !std::isfinite(static_cast<double>(n)))
        throw std::runtime_error("G4_REPLAY_EXPECT_FINITE_NUMBER");
    return static_cast<double>(n);
}

bool flag(const cv::FileNode &n, const char *key)
{
    if (!n[key].isInt() || (static_cast<int>(n[key]) != 0 && static_cast<int>(n[key]) != 1))
        throw std::runtime_error(std::string("G4_REPLAY_EXPECT_BOOL01:") + key);
    return static_cast<int>(n[key]) == 1;
}

bool absent(const cv::FileNode &n)
{
    return n.empty() || (n.isString() && static_cast<std::string>(n).empty());
}

template<int Rows, int Cols>
cv::Matx<double, Rows, Cols> matrix(const cv::FileNode &n)
{
    cv::Mat m;
    n >> m;
    if (m.rows != Rows || m.cols != Cols || m.type() != CV_64F || !cv::checkRange(m))
        throw std::runtime_error("G4_REPLAY_MATRIX_SHAPE_OR_FINITE");
    cv::Matx<double, Rows, Cols> result;
    m.copyTo(cv::Mat(result, false));
    return result;
}

cv::Vec3d vector3(const cv::FileNode &n)
{
    const auto m = matrix<3, 1>(n);
    return {m(0, 0), m(1, 0), m(2, 0)};
}

void write_stamp(cv::FileStorage &fs, const pose::CaptureStamp &s)
{
    fs << "{" << "session_id" << s.session_id_ << "camera_id" << s.camera_id_
       << "clock_domain" << s.clock_domain_ << "pixel_domain" << static_cast<int>(s.pixel_domain_)
       << "frame_id" << std::to_string(s.frame_id_) << "received_us" << std::to_string(s.received_us_)
       << "exposure_us" << (s.exposure_us_ ? std::to_string(*s.exposure_us_) : "")
       << "time_uncertainty_s";
    if (s.time_uncertainty_s_)
        fs << *s.time_uncertainty_s_;
    else
        fs << "";
    fs << "}";
}

pose::CaptureStamp read_stamp(const cv::FileNode &n)
{
    pose::CaptureStamp s;
    s.session_id_ = text(n, "session_id");
    s.camera_id_ = text(n, "camera_id");
    s.clock_domain_ = text(n, "clock_domain");
    if (!n["pixel_domain"].isInt() || static_cast<int>(n["pixel_domain"]) !=
        static_cast<int>(pose::PixelDomain::RawDistorted))
        throw std::runtime_error("G4_REPLAY_PIXEL_DOMAIN");
    s.pixel_domain_ = pose::PixelDomain::RawDistorted;
    s.frame_id_ = unsigned_integer(n["frame_id"]);
    s.received_us_ = signed_integer(n["received_us"]);
    if (!absent(n["exposure_us"]))
        s.exposure_us_ = signed_integer(n["exposure_us"]);
    if (!absent(n["time_uncertainty_s"]))
        s.time_uncertainty_s_ = number(n["time_uncertainty_s"]);
    return s;
}

pose::PoseStatus read_status(const cv::FileNode &n)
{
    const auto value = static_cast<std::string>(n);
    for (auto status : {pose::PoseStatus::InvalidInput, pose::PoseStatus::CorrespondenceMissing,
                       pose::PoseStatus::NotDetected, pose::PoseStatus::NoSolution,
                       pose::PoseStatus::BudgetMissing, pose::PoseStatus::Ambiguous,
                       pose::PoseStatus::Unique})
        if (value == pose::pose_status_name(status))
            return status;
    throw std::runtime_error("G4_REPLAY_UNKNOWN_POSE_STATUS");
}

void write_pose(cv::FileStorage &fs, const pose::PoseObservation &p)
{
    fs << "{" << "stamp";
    write_stamp(fs, p.stamp_);
    fs << "source" << p.source_ << "target_id" << p.target_id_
       << "detection_index" << std::to_string(p.detection_index_)
       << "status" << pose::pose_status_name(p.status_)
       << "corner_quality_flags" << int(p.corner_quality_flags_)
       << "control_usable" << static_cast<int>(p.control_usable_) << "candidates" << "[";
    for (const auto &c : p.candidates_)
    {
        fs << "{" << "rvec" << cv::Mat(c.pose_.rvec_) << "t_m" << cv::Mat(c.pose_.t_m_)
           << "metrics_valid" << static_cast<int>(c.metrics_valid_) << "eligible" << static_cast<int>(c.eligible_)
           << "rmse_px" << c.rmse_px_ << "max_error_px" << c.max_error_px_
           << "min_depth_m" << c.min_depth_m_ << "reasons" << "[";
        for (const auto &reason : c.reasons_) fs << reason;
        fs << "]" << "}";
    }
    fs << "]";
    if (p.selected_index_)
        fs << "selected_index" << std::to_string(*p.selected_index_);
    if (p.T_camera_target_)
        fs << "rvec" << cv::Mat(p.T_camera_target_->rvec_) << "t_m" << cv::Mat(p.T_camera_target_->t_m_);
    fs << "reasons" << "[";
    for (const auto &reason : p.reasons_)
        fs << reason;
    fs << "]" << "}";
}

// 兼容读取只恢复原文件确实保存的字段；旧covariance_available不能还原协方差数值。
pose::PoseObservation read_pose(const cv::FileNode &n, const pose::CaptureStamp &stamp, bool legacy)
{
    pose::PoseObservation p;
    p.stamp_ = legacy ? stamp : read_stamp(n["stamp"]);
    p.source_ = text(n, "source");
    p.target_id_ = text(n, "target_id");
    p.detection_index_ = unsigned_integer(n["detection_index"]);
    p.status_ = read_status(n["status"]);
    if (!n["corner_quality_flags"].empty()) p.corner_quality_flags_ = unsigned_integer(n["corner_quality_flags"]);
    p.control_usable_ = !legacy && flag(n, "control_usable");
    if (n["reasons"].isSeq())
        for (const auto &reason : n["reasons"])
            p.reasons_.push_back(static_cast<std::string>(reason));
    // 非Unique帧只恢复拒绝状态；不解析/使用可能非有限的失败候选，原文件作为证据保留。
    if (p.status_ != pose::PoseStatus::Unique)
        return p;
    if (!n["candidates"].isSeq())
        throw std::runtime_error("G4_REPLAY_CANDIDATES_NOT_SEQUENCE");
    for (const auto &c : n["candidates"])
    {
        pose::PoseCandidate candidate;
        candidate.metrics_valid_ = flag(c, "metrics_valid");
        candidate.eligible_ = flag(c, "eligible");
        if (c["reasons"].isSeq())
            for (const auto &reason : c["reasons"])
                candidate.reasons_.push_back(static_cast<std::string>(reason));
        if (candidate.eligible_)
        {
            candidate.pose_.rvec_ = vector3(c["rvec"]);
            candidate.pose_.t_m_ = vector3(c["t_m"]);
        }
        if (!c["rmse_px"].empty()) candidate.rmse_px_ = number(c["rmse_px"]);
        if (!c["max_error_px"].empty()) candidate.max_error_px_ = number(c["max_error_px"]);
        if (!c["min_depth_m"].empty()) candidate.min_depth_m_ = number(c["min_depth_m"]);
        p.candidates_.push_back(candidate);
    }
    if (!n["selected_index"].empty())
        p.selected_index_ = unsigned_integer(n["selected_index"]);
    if (!n["t_m"].empty())
        p.T_camera_target_ = pose::RigidPose{vector3(n["rvec"]), vector3(n["t_m"])};
    return p;
}
} // namespace

// native v4完整保存时间与旁路；legacy v1只允许回放，不能伪造曝光/硬件资格。
std::vector<TrackingFrame> read_tracking_frames(const std::filesystem::path &path)
{
    cv::FileStorage fs(path.string(), cv::FileStorage::READ);
    if (!fs.isOpened() || !fs["schema_version"].isInt() || !fs["frames"].isSeq())
        throw std::runtime_error("G4_REPLAY_OPEN_OR_SCHEMA_FAILED");
    const int version = static_cast<int>(fs["schema_version"]);
    if (version != 1 && version != 4)
        throw std::runtime_error("G4_REPLAY_UNSUPPORTED_VERSION");
    if (version == 4 && (text(fs.root(), "position_unit") != "m" ||
                         text(fs.root(), "time_unit") != "us"))
        throw std::runtime_error("G4_REPLAY_UNIT_MISMATCH");
    std::vector<TrackingFrame> result;
    for (const auto &n : fs["frames"])
    {
        TrackingFrame f;
        if (version == 1)
        {
            f.origin_ = InputOrigin::LegacyG2Replay;
            f.stamp_.session_id_ = text(fs.root(), "session_id");
            f.stamp_.camera_id_ = text(fs.root(), "camera_id");
            f.stamp_.clock_domain_ = text(fs.root(), "clock_domain");
            f.stamp_.pixel_domain_ = pose::PixelDomain::RawDistorted;
            f.stamp_.frame_id_ = unsigned_integer(n["frame_id"]);
            f.stamp_.received_us_ = signed_integer(n["timestamp_us"]);
            if (!n["mark"].isSeq())
                throw std::runtime_error("G4_LEGACY_MARK_NOT_SEQUENCE");
            for (const auto &p : n["mark"])
                f.observations_.push_back(read_pose(p, f.stamp_, true));
        }
        else
        {
            f.stamp_ = read_stamp(n["stamp"]);
            if (!n["origin"].isInt() || static_cast<int>(n["origin"]) < 0 ||
                static_cast<int>(n["origin"]) > 2 || !n["observations"].isSeq() || !n["position_noise"].isSeq())
                throw std::runtime_error("G4_NATIVE_FRAME_SCHEMA");
            f.origin_ = static_cast<InputOrigin>(static_cast<int>(n["origin"]));
            for (const auto &p : n["observations"])
                f.observations_.push_back(read_pose(p, f.stamp_, false));
            for (const auto &noise : n["position_noise"])
            {
                PositionNoise p;
                p.detection_index_ = unsigned_integer(noise["detection_index"]);
                p.camera_mse_m2_ = matrix<3, 3>(noise["camera_mse_m2"]);
                p.evidence_id_ = text(noise, "evidence_id");
                f.position_noise_.push_back(p);
            }
            if (!n["camera_transform"].empty())
            {
                const auto t = n["camera_transform"];
                CameraTransform transform;
                transform.stamp_ = read_stamp(t["stamp"]);
                transform.world_frame_id_ = text(t, "world_frame_id");
                transform.R_world_camera_ = matrix<3, 3>(t["R_world_camera"]);
                transform.t_world_camera_m_ = vector3(t["t_world_camera_m"]);
                if (!t["covariance"].empty()) transform.covariance_ = matrix<6, 6>(t["covariance"]);
                f.camera_transform_ = transform;
            }
        }
        result.push_back(std::move(f));
    }
    if (result.empty())
        throw std::runtime_error("G4_REPLAY_EMPTY_INPUT_REJECTED");
    return result;
}

void write_tracking_frames(const std::vector<TrackingFrame> &frames, const std::filesystem::path &path)
{
    if (frames.empty()) throw std::runtime_error("G4_CANNOT_WRITE_EMPTY_DATASET");
    cv::FileStorage fs(path.string(), cv::FileStorage::WRITE);
    if (!fs.isOpened()) throw std::runtime_error("G4_FRAME_WRITE_FAILED");
    fs << "schema_version" << 4 << "stream_type" << "G4PoseReplay"
       << "position_unit" << "m" << "time_unit" << "us" << "frames" << "[";
    for (const auto &f : frames) write_tracking_frame(fs, f);
    fs << "]";
}

void write_tracking_frame(cv::FileStorage &fs, const TrackingFrame &f) {
        fs << "{" << "stamp";
        write_stamp(fs, f.stamp_);
        fs << "origin" << static_cast<int>(f.origin_) << "observations" << "[";
        for (const auto &p : f.observations_) write_pose(fs, p);
        fs << "]" << "position_noise" << "[";
        for (const auto &p : f.position_noise_)
            fs << "{" << "detection_index" << std::to_string(p.detection_index_)
               << "camera_mse_m2" << cv::Mat(p.camera_mse_m2_) << "evidence_id" << p.evidence_id_
               << "}";
        fs << "]";
        if (f.camera_transform_)
        {
            const auto &t = *f.camera_transform_;
            fs << "camera_transform" << "{" << "stamp";
            write_stamp(fs, t.stamp_);
            fs << "world_frame_id" << t.world_frame_id_ << "R_world_camera" << cv::Mat(t.R_world_camera_)
               << "t_world_camera_m" << cv::Mat(t.t_world_camera_m_);
            if (t.covariance_) fs << "covariance" << cv::Mat(*t.covariance_);
            fs << "}";
        }
        fs << "}";
}

// 每帧连同拒绝事件写出，LOST不补旧位置；时间/资格/协方差口径随结果保存。
void write_tracking_results(const std::vector<TrackingResult> &results, const std::filesystem::path &path)
{
    if (results.empty()) throw std::runtime_error("G4_CANNOT_WRITE_EMPTY_RESULT");
    cv::FileStorage fs(path.string(), cv::FileStorage::WRITE);
    if (!fs.isOpened()) throw std::runtime_error("G4_RESULT_WRITE_FAILED");
    fs << "schema_version" << 1 << "stream_type" << "G4TrackingResult" << "frames" << "[";
    for (const auto &r : results) write_tracking_result(fs, r);
    fs << "]";
}
} // namespace mark::tracking

namespace mark::tracking {
void write_tracking_result(cv::FileStorage &fs, const TrackingResult &r) {
        fs << "{" << "stamp";
        write_stamp(fs, r.input_stamp_);
        fs << "input_accepted" << static_cast<int>(r.input_accepted_) << "ready" << static_cast<int>(r.ready_)
           << "events" << "[";
        for (const auto &event : r.events_) fs << event;
        fs << "]";
        if (r.association_nis_) fs << "association_nis" << *r.association_nis_;
        fs << "tracks" << "[";
        for (const auto &t : r.tracks_)
        {
            fs << "{" << "track_id" << std::to_string(t.track_id_) << "state" << track_state_name(t.state_)
               << "source" << t.source_ << "target_class" << t.target_class_
               << "coordinate_frame" << t.coordinate_frame_ << "velocity_meaning" << t.velocity_meaning_
               << "state_time_us" << std::to_string(t.state_time_us_) << "last_measurement_stamp";
            write_stamp(fs, t.last_measurement_stamp_);
            if (t.last_detection_index_)
                fs << "last_detection_index" << std::to_string(*t.last_detection_index_);
            fs << "confirmed" << static_cast<int>(t.confirmed_) << "time_bounded" << static_cast<int>(t.time_bounded_)
               << "qualified_for_g5" << static_cast<int>(t.qualified_for_g5_)
               << "uncertainty_scope" << t.uncertainty_scope_;
            if (t.position_m_) fs << "position_m" << cv::Mat(*t.position_m_);
            if (t.velocity_mps_) fs << "velocity_mps" << cv::Mat(*t.velocity_mps_);
            if (t.covariance_) fs << "covariance" << cv::Mat(*t.covariance_);
            if (t.observation_age_s_) fs << "observation_age_s" << *t.observation_age_s_;
            if (t.input_time_bound_s_) fs << "input_time_bound_s" << *t.input_time_bound_s_;
            fs << "}";
        }
        fs << "]" << "}";
}
}
