#include "vision/config/tracking_config.hpp"
#include "vision/math/covariance_validation.hpp"
#include "reader/value_reader.hpp"
#include <stdexcept>

namespace mark::tracking
{
    namespace
    {
        void bad(config_value::Error)
        {
            throw std::runtime_error("TRACK_CONFIG_VALUE_INVALID");
        }

        cv::FileNode required(const cv::FileNode &n, const char *k)
        {
            return config_value::required(n, k, bad);
        }

        std::string text(const cv::FileNode &n, const char *k)
        {
            return config_value::text(required(n, k), bad);
        }

        double number(const cv::FileNode &n, const char *k)
        {
            return config_value::number(required(n, k),
                                        config_value::NumericPolicy::CompleteNumericString, bad);
        }

    }

    TrackingConfig load_tracking_config(const std::filesystem::path &path)
    {
        cv::FileStorage fs(path.string(), cv::FileStorage::READ);
        if (!fs.isOpened() ||
            config_value::integer(required(fs.root(), "schema_version"), bad) != 2)
            throw std::runtime_error("TRACK_CONFIG_SCHEMA");
        for (const auto &n : fs.root())
            if (n.name() != "schema_version" && n.name() != "source" &&
                n.name() != "target_class" && n.name() != "stamp_uncertainty_semantics" &&
                n.name() != "profile")
                throw std::runtime_error("TRACK_CONFIG_UNKNOWN_KEY:" + n.name());

        TrackingConfig c;
        c.source_ = text(fs.root(), "source");
        c.target_class_ = text(fs.root(), "target_class");
        c.stamp_uncertainty_semantics_ = text(fs.root(), "stamp_uncertainty_semantics");
        const auto profile = text(fs.root(), "profile");
        if (!profile.empty())
        {
            cv::FileStorage p((path.parent_path() / profile).string(), cv::FileStorage::READ);
            if (!p.isOpened())
                throw std::runtime_error("TRACK_PROFILE_OPEN_FAILED");
            TrackingProfile model;
            cv::Mat r;
            required(p.root(), "camera_mse_m2") >> r;
            if (r.type() != CV_64F || r.rows != 3 || r.cols != 3)
                throw std::runtime_error("TRACK_PROFILE_R_SHAPE");
            r.copyTo(cv::Mat(model.camera_mse_m2_, false));
            model.acceleration_psd_m2ps3_ = number(p.root(), "acceleration_psd_m2ps3");
            model.initial_velocity_variance_m2ps2_ =
                number(p.root(), "initial_velocity_variance_m2ps2");
            c.profile_ = model;
        }
        validate_tracking_config(c);
        return c;
    }

    void validate_tracking_config(const TrackingConfig &c)
    {
        if (c.source_.empty() || c.target_class_.empty() ||
            (c.stamp_uncertainty_semantics_ != "unknown" &&
             c.stamp_uncertainty_semantics_ != "absolute_bound"))
            throw std::runtime_error("TRACK_CONFIG_INVALID");
        if (c.profile_)
        {
            const auto &p = *c.profile_;
            if (!observed::positive_definite(cv::Mat(p.camera_mse_m2_)) ||
                !std::isfinite(p.acceleration_psd_m2ps3_) || p.acceleration_psd_m2ps3_ < 0 ||
                !std::isfinite(p.initial_velocity_variance_m2ps2_) ||
                p.initial_velocity_variance_m2ps2_ <= 0)
                throw std::runtime_error("TRACK_PROFILE_NUMERICAL_INVALID");
        }
    }

}
