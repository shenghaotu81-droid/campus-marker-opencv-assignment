#include "vision/config/tracking_config.hpp"
#include "reader/value_reader.hpp"
#include <stdexcept>

namespace mark::tracking
{
    // profile 是同目录文件路径；空路径表示只处理时间上下文、不产生数值轨迹。
    TrackingConfig load_tracking_config(const std::filesystem::path &path)
    {
        cv::FileStorage fs(path.string(), cv::FileStorage::READ);
        if (!fs.isOpened()) throw std::runtime_error("TRACK_CONFIG_OPEN_FAILED:" + path.string());
        TrackingConfig c;
        c.source_ = config_value::text(fs["source"]);
        c.target_class_ = config_value::text(fs["target_class"]);
        c.stamp_uncertainty_semantics_ = config_value::text(fs["stamp_uncertainty_semantics"]);
        const auto profile = config_value::text(fs["profile"]);
        if (!profile.empty())
        {
            cv::FileStorage p((path.parent_path() / profile).string(), cv::FileStorage::READ);
            if (!p.isOpened()) throw std::runtime_error("TRACK_PROFILE_OPEN_FAILED");
            TrackingProfile model;
            cv::Mat r;
            p["camera_mse_m2"] >> r;
            r.copyTo(cv::Mat(model.camera_mse_m2_, false));
            model.acceleration_psd_m2ps3_ = config_value::number(p["acceleration_psd_m2ps3"]);
            model.initial_velocity_variance_m2ps2_ = config_value::number(p["initial_velocity_variance_m2ps2"]);
            c.profile_ = model;
        }
        return c;
    }
}
