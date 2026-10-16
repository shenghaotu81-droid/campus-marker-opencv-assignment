// 凸性/误差/正深度/旋转距离(自验证几何工具箱)
#include "pose/pose_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <opencv2/calib3d.hpp>

namespace mark::pose
{
// 状态名用于机器日志；中文说明由文档给出，避免调用方解析自然语言错误。
const char *pose_status_name(PoseStatus status_) noexcept
{
    switch (status_)
    {
    case PoseStatus::InvalidInput: return "INVALID_INPUT";
    case PoseStatus::CorrespondenceMissing: return "CORRESPONDENCE_MISSING";
    case PoseStatus::NotDetected: return "NOT_DETECTED";
    case PoseStatus::NoSolution: return "NO_SOLUTION";
    case PoseStatus::BudgetMissing: return "BUDGET_MISSING";
    case PoseStatus::Ambiguous: return "AMBIGUOUS";
    case PoseStatus::Unique: return "UNIQUE";
    }
    return "INVALID_INPUT";
}

namespace detail
{
// 时间戳合法性
// 跨会话/未知时钟不可视作同一时间轴；未知曝光与时间误差允许保留，但禁止控制放行。
bool valid_stamp(const CaptureStamp &s)
{
    return !s.session_id_.empty() && !s.camera_id_.empty() && !s.clock_domain_.empty() &&
           s.pixel_domain_ == PixelDomain::RawDistorted &&
           s.received_us_ >= 0 && (!s.exposure_us_ || (*s.exposure_us_ >= 0 &&
           *s.exposure_us_ <= s.received_us_)) && (!s.time_uncertainty_s_ ||
           (std::isfinite(*s.time_uncertainty_s_) && *s.time_uncertainty_s_ >= 0));
}

// 四边形在图像里多大（像素）：太小了（太远）位姿不准，min_quad_area_px2 预算是卡这个的
double quad_area(const std::array<cv::Point2d, 4> &p)
{
    double twice = 0;
    for (std::size_t i = 0; i < p.size(); ++i)
        twice += p[i].cross(p[(i + 1) % p.size()]);
    return std::abs(twice) / 2;
}

// 4 个 2D 点能否构成合法四边形（在图内、凸、不退化）
// 原图四角必须有限、在画面内、严格凸且按相邻物理角成环；退化图形不送 OpenCV。
bool valid_quad(const std::array<cv::Point2d, 4> &p, cv::Size size)
{
    if (size.width <= 0 || size.height <= 0)
        return false;
    double sign = 0;
    for (std::size_t i = 0; i < p.size(); ++i)
    {
        if (!std::isfinite(p[i].x) || !std::isfinite(p[i].y) || p[i].x < 0 || p[i].y < 0 ||
            p[i].x >= size.width || p[i].y >= size.height)
            return false;
        const auto a = p[(i + 1) % 4] - p[i];
        const auto b = p[(i + 2) % 4] - p[(i + 1) % 4];
        const double cross = a.cross(b);
        if (cross == 0 || (i > 0 && cross * sign <= 0))
            return false;
        sign = cross;
    }
    return quad_area(p) > 0;
}

// 两个旋转向量（rvec）差多少弧度？测试时验"算出的旋转对不对"，prior(PosePrior:一个"可信的历史位姿"，结构里有时间戳、目标 ID、位姿、trusted_unique_ 标志;只用来排除候选，不补角点、不预测。) 时比"新位姿跟上次差多少"(IPPE 对平面目标天生有双解歧义，没 prior 只能报 Ambiguous。G4 的 Kalman 滤波以后能给出预测位姿，那个就可以当可信 prior 来排歧(排除离得远的))
// 用旋转矩阵的夹角比较姿态，避免 Rodrigues 向量在 pi 附近直接相减的错误。
double rotation_distance(const cv::Vec3d &a, const cv::Vec3d &b)
{
    cv::Matx33d ra, rb;
    cv::Rodrigues(a, ra);
    cv::Rodrigues(b, rb);     // 旋转向量 ↔ 旋转矩阵互转
    const double cosine = (cv::trace(ra.t() * rb) - 1) / 2;        // 矩阵迹(旋转矩阵性质： trace(R) = 1 + 2 cos(theta))，算夹角余弦
    return std::acos(std::clamp(cosine, -1.0, 1.0));
}

// 对 PnP 结果的独立复核——拿算出的位姿把 3D 点重投影回图像，跟观测的 2D 点比，算 RMSE 和最大误差，检查正深度。不信求解器自己报的误差，自己重算一遍。
// OpenCV 自带误差的口径易混淆；统一复算每点二维欧氏误差的 RMS 和最大值。
PoseCandidate evaluate_pose(const RigidPose &pose_,
                            const std::array<cv::Point3d, 4> &object,
                            const std::array<cv::Point2d, 4> &image,
                            const cv::Mat &K_, const cv::Mat &dist_)
{
    PoseCandidate out;
    out.pose_ = pose_;
    out.min_depth_m_ = std::numeric_limits<double>::infinity();
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(pose_.rvec_[i]) || !std::isfinite(pose_.t_m_[i]))
        {
            out.reasons_.push_back("NONFINITE_POSE");
            return out;
        }
    cv::Matx33d r;
    cv::Rodrigues(pose_.rvec_, r);
    for (const auto &p : object)
        out.min_depth_m_ = std::min(out.min_depth_m_,
                                   (r * cv::Vec3d(p.x, p.y, p.z) + pose_.t_m_)[2]);
    if (!(out.min_depth_m_ > 0))
    {
        out.reasons_.push_back("NONPOSITIVE_DEPTH");
        return out;
    }
    std::vector<cv::Point2d> projected;
    cv::projectPoints(std::vector<cv::Point3d>(object.begin(), object.end()),
                      pose_.rvec_, pose_.t_m_, K_, dist_, projected);       // 3D 点按位姿投影回图像(重算误差)
    double sum = 0;
    for (std::size_t i = 0; i < image.size(); ++i)
    {
        const double e = cv::norm(projected[i] - image[i]);
        if (!std::isfinite(e))
        {
            out.reasons_.push_back("NONFINITE_PROJECTION");
            return out;
        }
        sum += e * e;
        out.max_error_px_ = std::max(out.max_error_px_, e);
    }
    out.rmse_px_ = std::sqrt(sum / image.size());
    out.metrics_valid_ = true;
    return out;
}
} // namespace detail
} // namespace mark::pose