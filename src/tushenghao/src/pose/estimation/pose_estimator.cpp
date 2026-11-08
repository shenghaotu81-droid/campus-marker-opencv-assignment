// 从同帧像素四角估计米制平面位姿，向跟踪输出名义平移及多解位置分歧。
#include "vision/pose/pose_estimator.hpp"
#include <vector>
#include "validation/pose_geometry.hpp"
#include <algorithm>
#include <opencv2/calib3d.hpp>
namespace mark::pose
{
    PoseEstimator::PoseEstimator(PoseConfig c) : config_(std::move(c))
    {
        config_.camera_.K_ = config_.camera_.K_.clone();
        config_.camera_.dist_ = config_.camera_.dist_.clone();
    }

    PoseObservation PoseEstimator::estimate_mark(const detector::FrameResult &raw, size_t index, cv::Size size, const CaptureStamp &stamp) const
    {
        PoseObservation out;
        out.stamp_ = stamp;
        out.source_ = "MARK";
        out.target_id_ = config_.mark_target_id_;
        out.detection_index_ = index;
        if (raw.frame_id_ != stamp.frame_id_ || raw.timestamp_us_ != stamp.exposure_us_.value_or(stamp.received_us_))
        {
            out.reason_ = "FRAME_STAMP_MISMATCH";
            return out;
        }
        if (raw.status_ != detector::Status::DETECTED)
        {
            if (raw.status_ != detector::Status::NOT_DETECTED) out.reason_ = "DETECTION_INPUT_INVALID";
            return out;
        }
        if (index >= raw.detections_.size())
        {
            out.reason_ = "DETECTION_INDEX_INVALID";
            return out;
        }
        const auto &d = raw.detections_[index];
        auto mapping = d.orientation_;
        std::sort(mapping.begin(), mapping.end());
        if (mapping != std::array<int, 4>{{0, 1, 2, 3}})
        {
            out.reason_ = "MAPPING_INVALID";
            return out;
        }
        std::array<cv::Point2d, 4> pixels;
        for (size_t i = 0; i < 4; ++i)
            pixels[i] = d.corners_[d.orientation_[i]];
        out = estimate_planar(config_.mark_points_m_, pixels, size, stamp, config_.mark_target_id_);
        out.source_ = "MARK";
        out.detection_index_ = index;
        return out;
    }

    PoseObservation PoseEstimator::estimate_planar(const std::array<cv::Point3d, 4> &object, const std::array<cv::Point2d, 4> &image_, cv::Size size, const CaptureStamp &stamp, const std::string &id) const
    {
        PoseObservation out;
        out.stamp_ = stamp;
        out.source_ = "PLANAR";
        out.target_id_ = id;
        if (id.empty() || stamp.camera_id_ != config_.camera_.camera_id_ || size != config_.camera_.image_size_ || !detail::valid_quad(image_, size))
        {
            out.reason_ = "STAMP_CAMERA_OR_QUAD_INVALID";
            return out;
        }

        for (size_t i = 0; i < 4; ++i)
        {
            auto p = object[i];
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.z != 0)
            {
                out.reason_ = "MODEL_INVALID";
                return out;
            }
        }
        // 合法像素四角与米制模型送入平面 PnP，候选由重投影和深度复核。
        std::vector<detail::PoseCandidate> candidates;
        try {
            std::vector<cv::Mat> rotations, translations;
            cv::solvePnPGeneric(std::vector<cv::Point3d>(object.begin(), object.end()), std::vector<cv::Point2d>(image_.begin(), image_.end()), config_.camera_.K_, config_.camera_.dist_, rotations, translations, false, cv::SOLVEPNP_IPPE);
            for (size_t i = 0; i < rotations.size(); ++i)
            {
                RigidPose p;
                for (int k = 0; k < 3; ++k)
                {
                    p.rvec_[k] = rotations[i].at<double>(k);
                    p.t_m_[k] = translations[i].at<double>(k);
                }
                auto c = detail::evaluate_pose(p, object, image_, config_.camera_.K_, config_.camera_.dist_);
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
        // 连续残差权重将求解器内部多解压缩为位置二阶矩，交给跟踪量测噪声。
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
