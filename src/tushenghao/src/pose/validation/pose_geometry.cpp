// 校验平面像点，并复算各候选的相机系深度和二维重投影 RMSE。
#include "validation/pose_geometry.hpp"
#include <vector>

#include <algorithm>
#include <cmath>
#include <limits>
#include <opencv2/calib3d.hpp>

namespace mark::pose
{
    namespace detail
    {
        // 角点须有限、在画面内，并按相邻角构成严格凸环。
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

        std::optional<PoseCandidate> evaluate_pose(const RigidPose &pose_,
                                                   const std::array<cv::Point3d, 4> &object,
                                                   const std::array<cv::Point2d, 4> &image_,
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
                min_depth = std::min(min_depth, (r * cv::Vec3d(p.x, p.y, p.z) + pose_.t_m_)[2]);

            // 检查累计最小深度，重投影阶段另查有限性。
            if (!(min_depth > 0))
            {
                return {};
            }

            std::vector<cv::Point2d> projected;

            cv::projectPoints(std::vector<cv::Point3d>(object.begin(), object.end()), pose_.rvec_,
                              pose_.t_m_, K_, dist_, projected);

            double sum = 0;

            for (std::size_t i = 0; i < image_.size(); ++i)
            {
                const double e = cv::norm(projected[i] - image_[i]);
                if (!std::isfinite(e))
                {
                    return {};
                }
                sum += e * e;
            }

            // 按点数归一化二维距离的平方和。
            out.rmse_px_ = std::sqrt(sum / image_.size());

            if (!std::isfinite(out.rmse_px_))
                return {};

            return out;
        }
    }
}
