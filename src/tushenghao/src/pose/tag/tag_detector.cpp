// 使用 AprilTag 36h11 检测出口标记，按已知尺寸估计平面位姿。
#include "vision/pose/tag_detector.hpp"
#include "validation/pose_geometry.hpp"
#include <opencv2/imgproc.hpp>
#include <apriltag/apriltag.h>
#include <apriltag/tag36h11.h>
#include <map>
#include <algorithm>
#include <cmath>

#include <stdexcept>

namespace mark::pose
{
    struct TagDetector::Impl
    {
        TagConfig config_;

        // C 后端对象由各自的销毁函数释放。
        std::unique_ptr<apriltag_family_t, decltype(&tag36h11_destroy)> family_{tag36h11_create(),
                                                                                tag36h11_destroy};

        std::unique_ptr<apriltag_detector_t, decltype(&apriltag_detector_destroy)> detector_{
            apriltag_detector_create(), apriltag_detector_destroy};

        explicit Impl(TagConfig c) : config_(std::move(c))
        {
            if (!family_ || !detector_)
                throw std::runtime_error("TAG_ALLOCATION_FAILED");

            detector_->nthreads = 1;
            detector_->quad_decimate = 1;
            detector_->quad_sigma = 0;
            // quad_decimate 为 1 时，refine_edges 不起作用。
            detector_->refine_edges = true;
            detector_->decode_sharpening = 0.25;

            apriltag_detector_add_family_bits(detector_.get(), family_.get(), 0);
        }
    };

    TagDetector::TagDetector(TagConfig c) : impl_(std::make_unique<Impl>(std::move(c)))
    {
    }

    TagDetector::~TagDetector() = default;

    // 只返回白名单内、角点合法且该 ID 在帧内唯一的观测。
    std::vector<TagObservation> TagDetector::detect(const cv::Mat &bgr, const CaptureStamp &stamp)
    {
        if (bgr.empty() || bgr.type() != CV_8UC3 || bgr.dims != 2)
            throw std::invalid_argument("TAG_INPUT_INVALID");

        cv::Mat gray;
        cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);

        // 借用灰度像素，灰度图在检测完成前保持有效。
        image_u8_t view{gray.cols, gray.rows, int(gray.step), gray.data};

        std::unique_ptr<zarray_t, decltype(&apriltag_detections_destroy)> ds(
            apriltag_detector_detect(impl_->detector_.get(), &view), apriltag_detections_destroy);

        if (!ds)
            throw std::runtime_error("TAG_BACKEND_FAILURE");

        std::vector<TagObservation> out;
        std::map<int, int> counts;

        for (int i = 0; i < zarray_size(ds.get()); ++i)
        {
            apriltag_detection_t *d = nullptr;

            zarray_get(ds.get(), i, &d);

            if (!impl_->config_.allowed_ids_.count(d->id))
                continue;

            TagObservation t;
            t.stamp_ = stamp;
            t.id_ = d->id;

            auto side = impl_->config_.side_m_.find(d->id);
            if (side != impl_->config_.side_m_.end())
                t.side_m_ = side->second;

            for (size_t p = 0; p < 4; ++p)
                t.corners_[p] = {d->p[p][0], d->p[p][1]};

            if (!detail::valid_quad(t.corners_, bgr.size()))
                continue;

            ++counts[t.id_];
            out.push_back(t);
        }

        // 将保留项移到前面，再删掉尾段。同 ID 的重复观测整组排除。
        out.erase(std::remove_if(out.begin(), out.end(),
                                 [&](const auto &t)
                                 {
                                     return counts[t.id_] != 1;
                                 }),
                  out.end());

        return out;
    }

    PoseObservation estimate_exit(const TagObservation &t, cv::Size size,
                                  const PoseEstimator &estimator)
    {
        if (!std::isfinite(t.side_m_) || t.side_m_ <= 0)
        {
            PoseObservation p;
            p.stamp_ = t.stamp_;
            p.source_ = "APRILTAG";
            p.reason_ = "METRIC_SIZE_MISSING_OR_INVALID";

            return p;
        }

        double h = t.side_m_ / 2;

        // Tag 模型原点在中心，点序与 AprilTag 返回的四角对应。
        std::array<cv::Point3d, 4> model{{{-h, h, 0}, {h, h, 0}, {h, -h, 0}, {-h, -h, 0}}};

        auto out = estimator.estimate_planar(model, t.corners_, size, t.stamp_);

        out.source_ = "APRILTAG";
        return out;
    }
}
