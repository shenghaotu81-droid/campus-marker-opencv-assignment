// 配置与加载接口
#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <opencv2/core.hpp>

namespace mark::pose
{
// 标定结果原来不带相机身份；身份与验证状态放新配置，不修改历史标定文件。
// K、dist、camera_id
struct CameraModel
{
    cv::Mat K_;
    cv::Mat dist_;
    cv::Size image_size_;
    std::string camera_id_;
    bool hardware_verified_{false};
};

// 四个空预算(G2 建好后有个 pose_sensitivity 工具，跑一遍生成 3456 组带真值的合成数据，看误差分布取 3σ，及均值)
struct PoseBudgets
{
    std::optional<double> max_rmse_px_;
    std::optional<double> max_corner_error_px_;
    std::optional<double> min_quad_area_px2_;
    std::optional<double> max_time_uncertainty_s_;   // 跟时间戳精度有关，硬件没到先空着
    bool hardware_verified_{false};
};

struct SelectionConfig
{
    // 未知安装约束保持空，不通过默认法向/距离偷偷排除第二解。
    std::optional<double> min_z_m_;
    std::optional<double> max_z_m_;
    std::optional<double> min_normal_z_;
    std::optional<double> max_normal_z_;
    bool constraints_verified_{false};
    bool refine_lm_{false};
    bool prior_enabled_{false};
    std::optional<double> prior_max_age_s_;
    std::optional<double> prior_max_translation_m_;
    std::optional<double> prior_max_rotation_rad_;
};

struct TagConfig
{
    int nthreads_{1};
    double quad_decimate_{1};
    double quad_sigma_{0};
    bool refine_edges_{true};
    double decode_sharpening_{0.25};
    int max_hamming_{0};
    std::optional<double> min_decision_margin_;
    std::set<int> allowed_ids_;
    std::map<int, double> side_m_;
    bool hardware_verified_{false};
};

// 总配置
struct G2Config
{
    CameraModel camera_;
    std::array<cv::Point3d, 4> mark_points_m_;
    std::string mark_target_id_;
    bool mark_geometry_verified_{false};
    PoseBudgets budgets_;
    SelectionConfig selection_;
    TagConfig tag_;
};

// 相对资源一律相对配置所在目录；默认入口由调用方 __FILE__ 推导。
// 读 yaml 的入口和校验
G2Config load_pose_config(const std::filesystem::path &path);
void validate_pose_config(const G2Config &config);
} // namespace mark::pose