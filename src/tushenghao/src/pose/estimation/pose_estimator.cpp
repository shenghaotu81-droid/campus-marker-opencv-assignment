// 用标定内参求平面位姿，向跟踪提供名义平移和多解分歧。
#include "vision/pose/pose_estimator.hpp"
#include <vector>
#include "validation/pose_geometry.hpp"
#include <algorithm>
#include <opencv2/calib3d.hpp>

namespace mark::pose
{
    PoseEstimator::PoseEstimator(PoseConfig c) : config_(std::move(c))
    {
        // 独立复制标定矩阵，避免调用方修改同一块存储。
        config_.camera_.K_ = config_.camera_.K_.clone();
        config_.camera_.dist_ = config_.camera_.dist_.clone();
    }

    PoseObservation PoseEstimator::estimate_mark(const detector::FrameResult &raw, size_t index,
                                                 cv::Size size, const CaptureStamp &stamp) const
    {
        PoseObservation out;
        out.stamp_ = stamp;
        out.source_ = "MARK";
        out.detection_index_ = index;

        // 未检出时返回空估计，原因留空。
        if (raw.detections_.empty())
            return out;

        const auto &d = raw.detections_[index];

        std::array<cv::Point2d, 4> pixels;
        // 用安装映射恢复物理角顺序。配置中的 MARK 原点为模型左上角。
        for (size_t i = 0; i < 4; ++i)
            pixels[i] = d.corners_[d.orientation_[i]];

        out = estimate_planar(config_.mark_points_m_, pixels, size, stamp);

        out.source_ = "MARK";
        out.detection_index_ = index;

        return out;
    }

    PoseObservation PoseEstimator::estimate_planar(const std::array<cv::Point3d, 4> &object,
                                                   const std::array<cv::Point2d, 4> &image_,
                                                   cv::Size size, const CaptureStamp &stamp) const
    {
        PoseObservation out;
        out.stamp_ = stamp;
        out.source_ = "PLANAR";

        // 检查标定尺寸与实际像点顺序。MARK 的屏幕排序后仍可能出现坏角点环。
        if (size != config_.camera_.image_size_ || !detail::valid_quad(image_, size))
        {
            out.reason_ = "IMAGE_SIZE_OR_QUAD_INVALID";
            return out;
        }

        // 模型加载后仍须保证平面内坐标有限。
        for (size_t i = 0; i < 4; ++i)
        {
            auto p = object[i];
            if (!std::isfinite(p.x) || !std::isfinite(p.y))
            {
                out.reason_ = "MODEL_INVALID";
                return out;
            }
        }

        std::vector<detail::PoseCandidate> candidates;

        try
        {
            std::vector<cv::Mat> rotations, translations;

            // IPPE 最多给两个候选，按实际返回的候选逐一复核。
            cv::solvePnPGeneric(std::vector<cv::Point3d>(object.begin(), object.end()),
                                std::vector<cv::Point2d>(image_.begin(), image_.end()),
                                config_.camera_.K_, config_.camera_.dist_, rotations, translations,
                                false, cv::SOLVEPNP_IPPE);

            for (size_t i = 0; i < rotations.size(); ++i)
            {
                RigidPose p;

                // 读取 3×1 double 旋转和平移向量，Mat::at 的调试断言依构建模式而异。
                for (int k = 0; k < 3; ++k)
                {
                    p.rvec_[k] = rotations[i].at<double>(k);
                    p.t_m_[k] = translations[i].at<double>(k);
                }

                auto c = detail::evaluate_pose(p, object, image_, config_.camera_.K_,
                                               config_.camera_.dist_);
                if (c)
                    candidates.push_back(*c);
            }
        }
        catch (const cv::Exception &)
        {
            out.reason_ = "PNP_NUMERICAL_FAILURE";
            candidates.clear();
        }

        if (candidates.empty())
        {
            if (out.reason_.empty())
                out.reason_ = "PNP_NO_LEGAL_SOLUTION";
            return out;
        }

        // 最小重投影 RMSE 对应名义位姿。
        const auto best = std::min_element(candidates.begin(), candidates.end(),
                                           [](const auto &a, const auto &b)
                                           {
                                               return a.rmse_px_ < b.rmse_px_;
                                           });

        out.estimate_.emplace();
        out.estimate_->pose_ = best->pose_;
        out.estimate_->rmse_px_ = best->rmse_px_;

        // 用逆平方相对权重累加名义解周围的平移二阶矩。单解时为零。
        for (auto it = candidates.begin(); it != candidates.end(); ++it)
        {
            if (it == best)
                continue;
            // 先按较大 RMSE 缩放。两误差均为零时用相同权重。
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
