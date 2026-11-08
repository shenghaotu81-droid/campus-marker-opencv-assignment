#pragma once
#include <array>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <opencv2/core.hpp>

namespace mark::pose
{
    struct CameraModel
    {
        cv::Mat K_, dist_;
        cv::Size image_size_;
        std::string camera_id_;
    };

    struct TagConfig
    {
        std::set<int> allowed_ids_;
        std::map<int, double> side_m_;
    };

    struct PoseConfig
    {
        CameraModel camera_;
        std::array<cv::Point3d, 4> mark_points_m_;
        std::string mark_target_id_;
        TagConfig tag_;
    };

    PoseConfig load_pose_config(const std::filesystem::path &);
}
