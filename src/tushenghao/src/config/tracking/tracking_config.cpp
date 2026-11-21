#include "vision/config/tracking_config.hpp"
#include "reader/value_reader.hpp"
#include <stdexcept>
#include <opencv2/core.hpp>
#include <filesystem>

namespace mark::tracking
{
    // profile 是同目录文件路径；空路径表示只处理时间上下文、不产生数值轨迹。
    TrackingConfig load_tracking_config(const std::filesystem::path &path)
    {
        // 开文件 + 打不开就抛错
        cv::FileStorage fs(path.string(), cv::FileStorage::READ);
        if (!fs.isOpened())
            throw std::runtime_error("TRACK_CONFIG_OPEN_FAILED:" + path.string());
        // 先造个默认配置，再逐字段从 YAML 覆盖
        TrackingConfig c;
        c.source_ = config_value::text(fs["source"]);
        c.target_class_ = config_value::text(fs["target_class"]);
        c.stamp_uncertainty_semantics_ = config_value::text(fs["stamp_uncertainty_semantics"]);
        // YAML 套 YAML（第二个文件）
        const auto profile = config_value::text(fs["profile"]);
        if (!profile.empty())
        {
            cv::FileStorage p((path.parent_path() / profile).string(), cv::FileStorage::READ);
            if (!p.isOpened())
                throw std::runtime_error("TRACK_PROFILE_OPEN_FAILED");
            
            TrackingProfile model;
            cv::Mat r;  // model.camera_mse_m2_ 是 cv::Matx33d,>> 没有给 Matx33d 定义的版本
            p["camera_mse_m2"] >> r;
            r.copyTo(cv::Mat(model.camera_mse_m2_, false));  // 把 Matx33d 的数据包成一个 Mat 头（false = 不拷贝，只包一下），语句结束临时 Mat 头销毁；copyTo(深拷贝：先把目标调成和源一样的尺寸/类型；再把源的数据拷进目标的缓冲区) 再把 r 的数据拷进去（缓冲区：Matx33d 的存储）
            model.acceleration_psd_m2ps3_ = config_value::number(p["acceleration_psd_m2ps3"]);
            model.initial_velocity_variance_m2ps2_ =
                config_value::number(p["initial_velocity_variance_m2ps2"]);
            c.profile_ = model;
        }
        return c;
    }
}
