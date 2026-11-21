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
        // cv::Mat:OpenCV 的矩阵/图像容器(最基本的数据结构：一个多维稠密数组)，可存储任意类型的矩阵/图像数据，支持多通道、不同数据类型、不同维度等特性
        // 特性：Mat 是引用计数的“把手”——拷贝一个 Mat 很便宜（只复制头信息，数据共享），跟 shared_ptr 一个道理
        // K_：相机内参矩阵，3×3 的 double 矩阵。里面是 fx、fy（焦距，像素单位，焦距（像素）= 物理焦距（mm）/ 像素尺寸（mm））和 cx、cy（主点/光心，光轴与成像平面的交点，单位像素，理想情况下在图像中心）
        /*  K 的作用即针孔相机模型：把相机坐标系里的 3D 点 (X, Y, Z) 投到像素 (u, v)：过光心投影、按焦距缩放、再平移到主点
            u = fx · X/Z + cx
            v = fy · Y/Z + cy
            [ fx  0  cx ]
        K=  [ 0  fy  cy ]
            [ 0   0   1 ]
                  ⎡u⎤   ⎡ fx  0  cx⎤ ⎡X⎤
              Z · ⎢v⎥ = ⎢ 0  fy  cy⎥ ⎢Y⎥
                  ⎣1⎦   ⎣ 0   0   1⎦ ⎣Z⎦
        K 做的是线性部分（乘），除以 Z 做的是透视部分（投影）。
        */
        // dist_：畸变系数，1×5 的 double 矩阵。里面是 k1、k2、p1、p2、k3（径向畸变和切向畸变系数）
        /* 真实镜头不是理想针孔，会畸变，dist_ 用来描述畸变模型。
        径向畸变是由于镜头的径向不均匀性造成的（镜头边缘比中心厚/薄，影响随离图像中心的距离增大），直线在边缘会弯曲——桶形（k1<0，线往外鼓）或枕形（k1>0，线往里缩），模型是畸变后的点按 (1 + k1·r² + k2·r⁴ + k3·r⁶) 缩放，r 是离主点的距离。
        切向畸变是镜头和传感器不完全平行（镜头装歪了一点）造成的，点会沿切向偏移,模型是畸变后的点按 2·p1·x·y + p2·(r² + 2·x²) 或者 p1·(r² + 2·y²) + 2·p2·x·y 平移。
        OpenCV 的畸变模型：
            x_distorted = x * (1 + k1*r^2 + k2*r^4 + k3*r^6) + 2*p1*x*y + p2*(r^2 + 2*x^2)
            y_distorted = y * (1 + k1*r^2 + k2*r^4 + k3*r^6) + p1*(r^2 + 2*y^2) + 2*p2*x*y
            r^2 = x^2 + y^2
        */
        cv::Mat K_, dist_;
        // image_size_就是标定时的图像尺寸（宽×高），标定数据和图像尺寸（分辨率）需绑定
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
