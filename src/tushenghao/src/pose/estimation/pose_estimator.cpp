#include "vision/pose/pose_estimator.hpp"
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
            out.status_ = raw.status == Status::NOT_DETECTED ? PoseStatus::NotDetected : PoseStatus::InvalidInput;
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
                    out.candidates_.push_back(*c);
            }
        }
        catch (const cv::Exception &){
            out.reason_ = "PNP_NUMERICAL_FAILURE";
            out.candidates_.clear();
        }
        if (out.candidates_.empty())
        {
            out.status_ = PoseStatus::NoSolution;
            return out;
        }
        out.selected_index_ = size_t(std::min_element(out.candidates_.begin(), out.candidates_.end(), [](const auto &a, const auto &b)
        {
            return a.rmse_px_ < b.rmse_px_;
        }) - out.candidates_.begin());
        out.status_ = out.candidates_.size() == 1 ? PoseStatus::Unique : PoseStatus::Ambiguous;
        return out;
    }

}
