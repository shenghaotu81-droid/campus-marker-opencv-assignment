#include "vision/math/covariance_validation.hpp"
#include "vision/config/tracking_config.hpp"
#include "reader/value_reader.hpp"
#include "vision/math/observed_geometry.hpp"
#include <cmath>
#include <stdexcept>

namespace mark::tracking
{
namespace
{
cv::FileNode require(const cv::FileNode &node,const char *key)
{
    return config_value::required(node,key,[&](auto){throw std::runtime_error(std::string("G4_CONFIG_MISSING:")+key);});
}
std::string text(const cv::FileNode &node,const char *key)
{
    return config_value::text(require(node,key),[&](auto){throw std::runtime_error(std::string("G4_CONFIG_EXPECT_STRING:")+key);});
}
void number_error(config_value::Error error)
{
    throw std::runtime_error(error==config_value::Error::PartialNumber?"G4_CONFIG_PARTIAL_NUMBER":
        error==config_value::Error::NonFinite?"G4_CONFIG_NONFINITE":"G4_CONFIG_EXPECT_NUMBER");
}
double number(const cv::FileNode &node)
{
    return config_value::number(node,config_value::NumericPolicy::CompleteNumericString,number_error);
}
bool flag(const cv::FileNode &node,const char *key)
{
    return config_value::boolean(require(node,key),[&](auto){throw std::runtime_error(std::string("G4_CONFIG_EXPECT_BOOL01:")+key);});
}
std::optional<double> optional_number(const cv::FileNode &node,const char *key)
{
    return config_value::optional_number(require(node,key),config_value::NumericPolicy::CompleteNumericString,
        config_value::OptionalPolicy::EmptyStringIsUnknown,number_error);
}

void version(const cv::FileStorage &fs)
{
    const auto v = require(fs.root(), "schema_version");
    if (!v.isInt() || static_cast<int>(v) != 1)
        throw std::runtime_error("G4_CONFIG_SCHEMA_UNSUPPORTED");
}
} // namespace

// Profile是独立标定产物；不改G2配置，不将已有二维拟合阈值挪用作3D噪声。
TrackingProfile load_tracking_profile(const std::filesystem::path &path)
{
    cv::FileStorage fs(path.string(), cv::FileStorage::READ);
    if (!fs.isOpened())
        throw std::runtime_error("G4_PROFILE_OPEN_FAILED:" + path.string());
    version(fs);
    TrackingProfile p;
    cv::Mat r;
    require(fs.root(), "camera_mse_m2") >> r;
    if (r.type() != CV_64F || r.rows != 3 || r.cols != 3)
        throw std::runtime_error("G4_PROFILE_R_SHAPE");
    r.copyTo(cv::Mat(p.camera_mse_m2_, false));
    p.acceleration_psd_m2ps3_ = number(require(fs.root(), "acceleration_psd_m2ps3"));
    p.initial_velocity_variance_m2ps2_ = number(require(fs.root(), "initial_velocity_variance_m2ps2"));
    p.association_nis_limit_ = number(require(fs.root(), "association_nis_limit"));
    p.bootstrap_nis_limit_ = number(require(fs.root(), "bootstrap_nis_limit"));
    p.position_std_limit_m_ = number(require(fs.root(), "position_std_limit_m"));
    p.velocity_std_limit_mps_ = number(require(fs.root(), "velocity_std_limit_mps"));
    p.max_dt_s_ = number(require(fs.root(), "max_dt_s"));
    p.validated_time_bound_s_ = optional_number(fs.root(), "validated_time_bound_s");
    p.evidence_id_ = text(fs.root(), "evidence_id");
    p.camera_id_ = text(fs.root(), "camera_id");
    p.basis_ = text(fs.root(), "basis");
    p.coordinate_mode_ = text(fs.root(), "coordinate_mode");
    p.time_policy_ = text(fs.root(), "time_policy");
    p.hardware_verified_ = flag(fs.root(), "hardware_verified");
    validate_tracking_profile(p);
    return p;
}

// 有效配置产物可复读；未批准的硬件状态不会因写盘自动变为true。
void write_tracking_profile(const TrackingProfile &p, const std::filesystem::path &path)
{
    validate_tracking_profile(p);
    cv::FileStorage fs(path.string(), cv::FileStorage::WRITE);
    if (!fs.isOpened())
        throw std::runtime_error("G4_PROFILE_WRITE_FAILED");
    fs << "schema_version" << 1 << "camera_mse_m2" << cv::Mat(p.camera_mse_m2_)
       << "acceleration_psd_m2ps3" << p.acceleration_psd_m2ps3_
       << "initial_velocity_variance_m2ps2" << p.initial_velocity_variance_m2ps2_
       << "association_nis_limit" << p.association_nis_limit_
       << "bootstrap_nis_limit" << p.bootstrap_nis_limit_
       << "position_std_limit_m" << p.position_std_limit_m_
       << "velocity_std_limit_mps" << p.velocity_std_limit_mps_
       << "max_dt_s" << p.max_dt_s_ << "validated_time_bound_s";
    if (p.validated_time_bound_s_)
        fs << *p.validated_time_bound_s_;
    else
        fs << "";
    fs << "evidence_id" << p.evidence_id_ << "camera_id" << p.camera_id_
       << "basis" << p.basis_ << "coordinate_mode" << p.coordinate_mode_
       << "time_policy" << p.time_policy_
       << "hardware_verified" << static_cast<int>(p.hardware_verified_);
}

TrackingConfig load_tracking_config(const std::filesystem::path &path)
{
    cv::FileStorage fs(path.string(), cv::FileStorage::READ);
    if (!fs.isOpened())
        throw std::runtime_error("G4_CONFIG_OPEN_FAILED:" + path.string());
    version(fs);
    TrackingConfig c;
    c.source_ = text(fs.root(), "source");
    c.target_class_ = text(fs.root(), "target_class");
    const auto coordinate = text(fs.root(), "coordinate_mode");
    if (coordinate != "camera_relative" && coordinate != "local_world")
        throw std::runtime_error("G4_CONFIG_COORDINATE_MODE");
    c.coordinate_mode_ = coordinate == "camera_relative" ? CoordinateMode::CameraRelative :
                                                          CoordinateMode::LocalWorld;
    c.world_frame_id_ = text(fs.root(), "world_frame_id");
    c.time_policy_ = text(fs.root(), "time_policy");
    c.stamp_uncertainty_semantics_ = text(fs.root(), "stamp_uncertainty_semantics");
    c.max_time_uncertainty_s_ = optional_number(fs.root(), "max_time_uncertainty_s");
    const std::filesystem::path profile(text(fs.root(), "profile"));
    if (!profile.empty())
    {
        if (profile.is_absolute())
            throw std::runtime_error("G4_CONFIG_PROFILE_MUST_BE_RELATIVE");
        c.profile_ = load_tracking_profile((path.parent_path() / profile).lexically_normal());
    }
    validate_tracking_config(c);
    return c;
}

void validate_tracking_profile(const TrackingProfile &p)
{
    if (!observed::positive_definite(cv::Mat(p.camera_mse_m2_)))
        throw std::runtime_error("G4_PROFILE_R_NOT_PD");
    for (double value : {p.acceleration_psd_m2ps3_, p.initial_velocity_variance_m2ps2_,
                        p.association_nis_limit_, p.bootstrap_nis_limit_,
                        p.position_std_limit_m_, p.velocity_std_limit_mps_, p.max_dt_s_})
        if (!std::isfinite(value) || value <= 0)
            throw std::runtime_error("G4_PROFILE_NONPOSITIVE_VALUE");
    // 有界域必须显式声明单帧时间界；构造时拒绝缺项，避免重建途中访问空optional。
    if (p.time_policy_ == "bounded_only" && !p.validated_time_bound_s_)
        throw std::runtime_error("G4_PROFILE_BOUNDED_TIME_BOUND_MISSING");
    if (p.evidence_id_.empty() || p.basis_.empty() || (p.hardware_verified_ && p.camera_id_.empty()) ||
        (p.coordinate_mode_ != "camera_relative" && p.coordinate_mode_ != "local_world") ||
        (p.time_policy_ != "nominal_research" && p.time_policy_ != "bounded_only") ||
        (p.validated_time_bound_s_ && (!std::isfinite(*p.validated_time_bound_s_) ||
                                     *p.validated_time_bound_s_ < 0)))
        throw std::runtime_error("G4_PROFILE_NO_PROVENANCE_OR_TIME_BOUND");
}

void validate_tracking_config(const TrackingConfig &c)
{
    if (c.source_.empty() || c.target_class_.empty() || c.world_frame_id_.empty() ||
        (c.time_policy_ != "nominal_research" && c.time_policy_ != "bounded_only") ||
        (c.stamp_uncertainty_semantics_ != "unknown" &&
         c.stamp_uncertainty_semantics_ != "absolute_bound") ||
        (c.max_time_uncertainty_s_ && (!std::isfinite(*c.max_time_uncertainty_s_) ||
                                      *c.max_time_uncertainty_s_ < 0)))
        throw std::runtime_error("G4_CONFIG_INVALID");
    if (c.profile_)
    {
        validate_tracking_profile(*c.profile_);
        const auto mode = c.coordinate_mode_ == CoordinateMode::LocalWorld ? "local_world" : "camera_relative";
        if (c.profile_->hardware_verified_ &&
            (c.profile_->coordinate_mode_ != mode || c.profile_->time_policy_ != c.time_policy_))
            throw std::runtime_error("G4_PROFILE_COORDINATE_DOMAIN_MISMATCH");
    }
}
} // namespace mark::tracking
