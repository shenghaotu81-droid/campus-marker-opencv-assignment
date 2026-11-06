// 凸性/误差/正深度/旋转距离(自验证几何工具箱)
#include "validation/pose_geometry.hpp"
#include "vision/config/pose_config.hpp"

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
        case PoseStatus::NotDetected: return "NOT_DETECTED";
        case PoseStatus::NoSolution: return "NO_SOLUTION";
        case PoseStatus::Ambiguous: return "AMBIGUOUS";
        case PoseStatus::Unique: return "UNIQUE";
        }
        return "INVALID_INPUT";
    }

    namespace detail
    {
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
                if (!std::isfinite(cross) || cross == 0 || (i > 0 && cross * sign <= 0))
                    return false;
                sign = cross;
            }
            return true;
        }

// 对 PnP 结果的独立复核——拿算出的位姿把 3D 点重投影回图像，跟观测的 2D 点比，算 RMSE ，检查正深度。不信求解器自己报的误差，自己重算一遍。
// OpenCV 自带误差的口径易混淆；统一复算每点二维欧氏误差的 RMS。
        std::optional<PoseCandidate> evaluate_pose(const RigidPose &pose_,
                                                   const std::array<cv::Point3d, 4> &object,
                                                   const std::array<cv::Point2d, 4> &image,
                                                   const cv::Mat &K_, const cv::Mat &dist_)
        {
            PoseCandidate out;
            out.pose_ = pose_;
            double min_depth = std::numeric_limits<double>::infinity();
            for (int i = 0; i < 3; ++i)
                if (!std::isfinite(pose_.rvec_[i]) || !std::isfinite(pose_.t_m_[i]))
                {
                    return {};
                }
            cv::Matx33d r;
            cv::Rodrigues(pose_.rvec_, r);
            for (const auto &p : object)
                min_depth = std::min(min_depth,
                                     (r * cv::Vec3d(p.x, p.y, p.z) + pose_.t_m_)[2]);
            if (!(min_depth > 0))
            {
                return {};
            }
            std::vector<cv::Point2d> projected;
            cv::projectPoints(std::vector<cv::Point3d>(object.begin(), object.end()),
                              pose_.rvec_, pose_.t_m_, K_, dist_, projected); // 3D 点按位姿投影回图像(重算误差)
            double sum = 0;
            for (std::size_t i = 0; i < image.size(); ++i)
            {
                const double e = cv::norm(projected[i] - image[i]);
                if (!std::isfinite(e))
                {
                    return {};
                }
                sum += e * e;
            }
            out.rmse_px_ = std::sqrt(sum / image.size());
            if (!std::isfinite(out.rmse_px_))
                return {};
            return out;
        }

    } // namespace detail
} // namespace mark::pose
