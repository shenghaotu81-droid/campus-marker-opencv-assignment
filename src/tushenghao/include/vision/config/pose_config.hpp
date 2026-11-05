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
// 标定结果原来不带相机身份；身份放配置，不修改历史标定文件。
// K、dist、camera_id
struct CameraModel
{
    cv::Mat K_;
    cv::Mat dist_;
    cv::Size image_size_;
    std::string camera_id_;
};

// 重投影预算；按相机 fx 比例缩放，在板子录像上验证分布。
struct PoseBudgets
{
    std::optional<double> max_rmse_px_;
    std::optional<double> max_corner_error_px_;
};

struct SelectionConfig
{
    // 可空表示沿用原筛选；有值时，两支物理合法解须充分分离才发布Unique。
    // 只做候选消歧，不将小差距整帧拒绝为NoSolution，单位原图px。
    std::optional<double> min_rmse_gap_px_;
    // 未知安装约束保持空，不通过默认法向/距离偷偷排除第二解。
    std::optional<double> min_z_m_;
    std::optional<double> max_z_m_;
    std::optional<double> min_normal_z_;
    std::optional<double> max_normal_z_;
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
};

// 总配置
struct G2Config
{
    CameraModel camera_;
    std::array<cv::Point3d, 4> mark_points_m_;
    std::string mark_target_id_;
    PoseBudgets budgets_;
    SelectionConfig selection_;
    TagConfig tag_;
};

// 相对资源一律相对配置所在目录；默认入口由调用方 __FILE__ 推导。
// 读 yaml 的入口和校验
G2Config load_pose_config(const std::filesystem::path &path);
void validate_pose_config(const G2Config &config);
} // namespace mark::pose
