#include "vision/config/pose_config.hpp"
#include "reader/value_reader.hpp"
#include <stdexcept>

namespace mark::pose
{
    // 内参从同目录标定文件读取，模型点按物理角顺序转成米供 PnP 使用。
    PoseConfig load_pose_config(const std::filesystem::path &path)
    {
        cv::FileStorage fs(path.string(), cv::FileStorage::READ);
        if (!fs.isOpened()) throw std::runtime_error("POSE_CONFIG_OPEN_FAILED:" + path.string());
        PoseConfig c;
        const auto camera = fs["camera"], geometry = fs["geometry"], tag = fs["tag"];
        const auto calibration = path.parent_path() / config_value::text(camera["calibration"]);
        cv::FileStorage cal(calibration.string(), cv::FileStorage::READ);
        if (!cal.isOpened()) throw std::runtime_error("POSE_CALIBRATION_OPEN_FAILED:" + calibration.string());
        cal["camera_matrix"] >> c.camera_.K_;
        cal["distortion_coefficients"] >> c.camera_.dist_;
        c.camera_.image_size_ = {config_value::integer(cal["image_width"]),
                                 config_value::integer(cal["image_height"])};
        c.camera_.camera_id_ = config_value::text(camera["camera_id"]);
        const double scale = config_value::text(geometry["source_unit"]) == "mm" ? .001 : 1.;
        const auto points = geometry["model_points"];
        for (int i = 0; i < 4; ++i)
            c.mark_points_m_[i] = {config_value::number(points[i][0]) * scale,
                                   config_value::number(points[i][1]) * scale, 0};
        c.mark_target_id_ = config_value::text(geometry["target_id"]);
        for (const auto &id : tag["allowed_ids"])
            c.tag_.allowed_ids_.insert(config_value::integer(id));
        for (const auto &size : tag["sizes"])
            c.tag_.side_m_.emplace(config_value::integer(size["id"]), config_value::number(size["side_m"]));
        return c;
    }
}
