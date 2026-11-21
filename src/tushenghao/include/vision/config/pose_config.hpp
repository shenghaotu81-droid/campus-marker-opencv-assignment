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
        // 标定内参 K 使用像素单位，dist 保存对应的畸变系数。
        cv::Mat K_, dist_;
        // 求解图像尺寸必须与标定尺寸一致。
        cv::Size image_size_;
    };

    struct TagConfig
    {
        // set 是“无重复、有序的集合”；存允许的 tag ID，只有在这个集合里的 tag 才会被接受（“有没有这个 ID”：去重 + 快速查找 .count(id)）
        std::set<int> allowed_ids_;
        // map 是“键值对集合”，按 key 排序；存 tag ID 到边长（米）的映射，解 tag 的位姿需要知道它的真实边长（“给一个 ID，查它的边长”：side_m_[id] 查字典）
        std::map<int, double> side_m_;
    };

    struct PoseConfig
    {
        // 相机模型
        CameraModel camera_;
        // MARK 的 4 个角点在 MARK 自身平面里的 3D 坐标（米）；cv::Point3d 是 3 个 double (x, y, z)；solvePnP 的“3D 模型点”
        std::array<cv::Point3d, 4> mark_points_m_;
        // AprilTag 配置
        TagConfig tag_;
    };

    // 声明：传 pose.yaml 的路径，返回一个填好的 PoseConfig
    PoseConfig load_pose_config(const std::filesystem::path &);
}
