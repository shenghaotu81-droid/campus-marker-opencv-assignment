// 来源/位姿/Tag/退出观测
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <opencv2/core.hpp>

namespace mark::pose
{
enum class PixelDomain { Unknown, RawDistorted };
// 原检测接口只有一个时间戳；旁路记录真实来源，未知曝光时间不能冒充已知。
struct CaptureStamp
{
    std::string session_id_;
    std::string camera_id_;
    std::string clock_domain_;
    PixelDomain pixel_domain_{PixelDomain::Unknown};
    std::uint64_t frame_id_{0};
    std::int64_t received_us_{0};
    std::optional<std::int64_t> exposure_us_;
    std::optional<double> time_uncertainty_s_;
};

// 统一表示目标坐标到相机坐标：X_camera = R * X_target + t，平移单位 m。
struct RigidPose
{
    cv::Vec3d rvec_{0, 0, 0};
    cv::Vec3d t_m_{0, 0, 0};
};

enum class PoseStatus
{
    InvalidInput,
    CorrespondenceMissing,
    NotDetected,
    NoSolution,
    BudgetMissing,
    Ambiguous,
    Unique      // 唯一可信状态
};

// 原先只有二值检测；候选逐项保留测量值与拒绝原因，不把残差包装为概率。
struct PoseCandidate
{
    RigidPose pose_;
    double rmse_px_{0};
    double max_error_px_{0};
    double min_depth_m_{0};
    bool metrics_valid_{false};
    bool eligible_{false};
    std::vector<std::string> reasons_;
};

struct PoseObservation
{
    CaptureStamp stamp_;
    std::string source_;
    std::string target_id_;
    std::size_t detection_index_{0};
    PoseStatus status_{PoseStatus::InvalidInput};
    std::vector<PoseCandidate> candidates_;
    std::optional<std::size_t> selected_index_;
    std::optional<RigidPose> T_camera_target_;
    std::optional<double> selected_rmse_px_;
    std::optional<double> refined_rmse_px_;
    // 未标定不确定度时保持空；零矩阵会错误宣称没有不确定性。
    std::optional<std::array<double, 36>> covariance_se3_;
    bool control_usable_{false};
    std::vector<std::string> reasons_;
};

// 历史只作候选排除证据；不补当前角点、不预测、不在求解器中自动积累状态。
struct PosePrior
{
    CaptureStamp stamp_;
    std::string target_id_;
    RigidPose pose_;
    bool trusted_unique_{false};
};

// 保留 AprilTag 的规范解码角序 p[0..3]；它不是 MARK 的屏幕 LT/RT/RB/LB。
struct TagObservation
{
    CaptureStamp stamp_;
    std::string family_{"tag36h11"};
    int id_{-1};
    int hamming_{0};
    double decision_margin_{0};
    std::array<cv::Point2d, 4> corners_;
    bool whitelisted_{false};
    bool decode_accepted_{false};
    std::vector<std::string> reasons_;
};

// 退出感知不等于任务退出授权；RFID/权限/后退动作属于后续决策层。
struct ExitObservation
{
    TagObservation tag_;
    PoseObservation pose_;
    bool alignment_measurement_usable_{false};
};

const char *pose_status_name(PoseStatus status_) noexcept;
} // namespace mark::pose