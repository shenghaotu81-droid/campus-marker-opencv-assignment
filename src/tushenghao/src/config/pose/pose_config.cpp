#include "vision/config/pose_config.hpp"
#include "reader/value_reader.hpp"
#include <stdexcept>
#include <opencv2/core.hpp>
#include <filesystem>
namespace mark::pose
{
    // 内参从同目录标定文件读取，模型点按物理角顺序转成米供 PnP 使用。
    // 读 pose.yaml——标定参数、MARK 几何、AprilTag 配置
    PoseConfig load_pose_config(const std::filesystem::path &path)
    {
        cv::FileStorage fs(path.string(), cv::FileStorage::READ);
        if (!fs.isOpened())
            throw std::runtime_error("POSE_CONFIG_OPEN_FAILED:" + path.string());
        
        PoseConfig c;
        // 把 pose.yaml 的三个顶层节点各取出来，存成三个短名字的变量；fs[...]返回一个 cv::FileNode——指向 YAML 里 "..." 那个节点的句柄（轻量引用，指向解析好的文档里的节点）
        const auto camera = fs["camera"], geometry = fs["geometry"], tag = fs["tag"];
        // parent_path() 取父目录路径；拼接：目录 / 文件名 = 完整路径
        const auto calibration = path.parent_path() / config_value::text(camera["calibration"]);
        cv::FileStorage cal(calibration.string(), cv::FileStorage::READ);
        if (!cal.isOpened())
            throw std::runtime_error("POSE_CALIBRATION_OPEN_FAILED:" + calibration.string());
        // >> 是 OpenCV 专门为 FileNode 重载的提取运算符，能把节点内容读进 Mat，还会按 YAML 里存的行数、列数、类型给 Mat 分配内存并填数据（K 读成 3×3 double，dist 读成 1×5 double）
        // FileNode（节点句柄） 没有到 cv::Mat 的隐式转换，只有到 int、double、string 这些基本类型的转换运算符（static_cast）
        cal["camera_matrix"] >> c.camera_.K_;
        cal["distortion_coefficients"] >> c.camera_.dist_;
        c.camera_.image_size_ = {config_value::integer(cal["image_width"]),
                                 config_value::integer(cal["image_height"])};
        c.camera_.camera_id_ = config_value::text(camera["camera_id"]);

        const double scale = config_value::text(geometry["source_unit"]) == "mm" ? 0.001 : 1.0;
        const auto points = geometry["model_points"];
        for (int i = 0; i < 4; ++i)
            c.mark_points_m_[i] = {config_value::number(points[i][0]) * scale,
                                   config_value::number(points[i][1]) * scale, 0};

        // insert() 是 set 的成员函数，插入一个元素，set 自己把它放到正确的排序位置，同时自动去重；emplace() 是 map 的成员函数，插入（就地构造）一个键值对（key-value pair）
        for (const auto &id : tag["allowed_ids"])
            c.tag_.allowed_ids_.insert(config_value::integer(id));
        /* sizes:
            - id:       
              side_m: 
        */
        for (const auto &size : tag["sizes"])
            c.tag_.side_m_.emplace(config_value::integer(size["id"]),
                                   config_value::number(size["side_m"]));
        return c;
    }
}
