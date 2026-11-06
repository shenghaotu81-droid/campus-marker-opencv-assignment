#include "vision/pose/pose_estimator.hpp"
#include <vector>
#include "validation/pose_geometry.hpp"
#include <algorithm>
#include <opencv2/calib3d.hpp>
namespace mark::pose
{
    PoseEstimator::PoseEstimator(G2Config c) : config_(std::move(c))
    {
        validate_pose_config(config_);
        config_.camera_.K_ = config_.camera_.K_.clone();
        config_.camera_.dist_ = config_.camera_.dist_.clone();
    }

    PoseObservation PoseEstimator::estimate_mark(const FrameResult &raw, size_t index, cv::Size size, const CaptureStamp &stamp) const
    {
        PoseObservation out;
        out.stamp_ = stamp;
        out.source_ = "MARK";
        out.target_id_ = config_.mark_target_id_;
        out.detection_index_ = index;
        if (raw.frame_id != stamp.frame_id_ || raw.timestamp_us != stamp.exposure_us_.value_or(stamp.received_us_))
        {
            out.reason_ = "FRAME_STAMP_MISMATCH";
            return out;
        }
        if (raw.status != Status::DETECTED)
        {
            if (raw.status != Status::NOT_DETECTED) out.reason_ = "DETECTION_INPUT_INVALID";
            return out;
        }
        if (index >= raw.detections.size())
        {
            out.reason_ = "DETECTION_INDEX_INVALID";
            return out;
        }
        const auto &d = raw.detections[index];
        auto mapping = d.orientation;
        std::sort(mapping.begin(), mapping.end());
        if (mapping != std::array<int, 4>{{0, 1, 2, 3}})
        {
            out.reason_ = "MAPPING_INVALID";
            return out;
        }
        std::array<cv::Point2d, 4> pixels;
        for (size_t i = 0; i < 4; ++i)
            pixels[i] = d.corners[d.orientation[i]];
        out = estimate_planar(config_.mark_points_m_, pixels, size, stamp, config_.mark_target_id_);
        out.source_ = "MARK";
        out.detection_index_ = index;
        return out;
    }

    PoseObservation PoseEstimator::estimate_planar(const std::array<cv::Point3d, 4> &object, const std::array<cv::Point2d, 4> &image, cv::Size size, const CaptureStamp &stamp, const std::string &id) const
    {
        PoseObservation out;
        out.stamp_ = stamp;
        out.source_ = "PLANAR";
        out.target_id_ = id;
        if (!stamp.valid() || id.empty() || stamp.camera_id_ != config_.camera_.camera_id_ || size != config_.camera_.image_size_ || !detail::valid_quad(image, size))
        {
            out.reason_ = "STAMP_CAMERA_OR_QUAD_INVALID";
            return out;
        }
        std::array<cv::Point2d, 4> xy;
        for (size_t i = 0; i < 4; ++i)
        {
            auto p = object[i];
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.z != 0)
            {
                out.reason_ = "MODEL_INVALID";
                return out;
            }
            xy[i] = {p.x, p.y};
        }
        // 只检查模型确有二维范围/凸环，不重复相机与不可变配置验证。
        double sign = 0;
        for (size_t i = 0; i < 4; ++i)
        {
            double cross = (xy[(i + 1) % 4] - xy[i]).cross(xy[(i + 2) % 4] - xy[(i + 1) % 4]);
            if (cross == 0 || (i && (cross > 0) != (sign > 0)))
            {
                out.reason_ = "MODEL_DEGENERATE";
                return out;
            }
            sign = cross;
        }
        std::vector<detail::PoseCandidate> candidates;
        try {
            std::vector<cv::Mat> rotations, translations;
            cv::solvePnPGeneric(std::vector<cv::Point3d>(object.begin(), object.end()), std::vector<cv::Point2d>(image.begin(), image.end()), config_.camera_.K_, config_.camera_.dist_, rotations, translations, false, cv::SOLVEPNP_IPPE);
            for (size_t i = 0; i < rotations.size(); ++i)
            {
                RigidPose p;
                for (int k = 0; k < 3; ++k)
                {
                    p.rvec_[k] = rotations[i].at<double>(k);
                    p.t_m_[k] = translations[i].at<double>(k);
                }
                auto c = detail::evaluate_pose(p, object, image, config_.camera_.K_, config_.camera_.dist_);
                if (c)
                    candidates.push_back(*c);
            }
        }
        catch (const cv::Exception &){
            out.reason_ = "PNP_NUMERICAL_FAILURE";
            candidates.clear();
        }
        if (candidates.empty())
        {
            if (out.reason_.empty()) out.reason_ = "PNP_NO_LEGAL_SOLUTION";
            return out;
        }
        const auto best = std::min_element(candidates.begin(), candidates.end(),
            [](const auto &a, const auto &b) { return a.rmse_px_ < b.rmse_px_; });
        out.estimate_.emplace();
        out.estimate_->pose_ = best->pose_;
        out.estimate_->rmse_px_ = best->rmse_px_;
        // 与此前G4中相同的连续残差权重；只把求解器内部多解收敛成位置二阶矩。
        for (auto it = candidates.begin(); it != candidates.end(); ++it)
        {
            if (it == best) continue;
            const double scale = std::max(best->rmse_px_, it->rmse_px_);
            const double a = scale > 0 ? best->rmse_px_ / scale : 1;
            const double b = scale > 0 ? it->rmse_px_ / scale : 1;
            const double weight = a * a / (a * a + b * b);
            const auto delta = it->pose_.t_m_ - best->pose_.t_m_;
            out.estimate_->translation_spread_m2_ += weight * (delta * delta.t());
        }
        return out;
    }

}
