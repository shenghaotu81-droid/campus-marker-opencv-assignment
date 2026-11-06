#include "vision/pose/tag_detector.hpp"
#include "validation/pose_geometry.hpp"
#include "pose/tag_config_validation.hpp"
#include <opencv2/imgproc.hpp>
#include <apriltag/apriltag.h>
#include <apriltag/tag36h11.h>
#include <map>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace mark::pose
{
    struct TagDetector::Impl
    {
        TagConfig config;
        std::unique_ptr<apriltag_family_t, decltype(&tag36h11_destroy)> family{tag36h11_create(), tag36h11_destroy};
        std::unique_ptr<apriltag_detector_t, decltype(&apriltag_detector_destroy)> detector{apriltag_detector_create(), apriltag_detector_destroy};
        explicit Impl(TagConfig c) : config(std::move(c))
        {
            if (!family || !detector || !detail::validateTagConfig(config))
                throw std::runtime_error("TAG_CONFIG_OR_ALLOCATION_INVALID");
            detector->nthreads = 1;
            detector->quad_decimate = 1;
            detector->quad_sigma = 0;
            detector->refine_edges = true;
            detector->decode_sharpening = .25;
            apriltag_detector_add_family_bits(detector.get(), family.get(), 0);
        }

    };
    TagDetector::TagDetector(TagConfig c) : impl_(std::make_unique<Impl>(std::move(c)))
    {
    }

    TagDetector::~TagDetector() = default;
    std::vector<TagObservation> TagDetector::detect(const cv::Mat &bgr, const CaptureStamp &stamp)
    {
        if (bgr.empty() || bgr.type() != CV_8UC3 || bgr.dims != 2 || !stamp.valid())
            throw std::invalid_argument("TAG_INPUT_INVALID");
        cv::Mat gray;
        cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
        if (gray.step > size_t(std::numeric_limits<int>::max()))
            throw std::invalid_argument("TAG_STRIDE_OVERFLOW");
        image_u8_t view{gray.cols, gray.rows, int(gray.step), gray.data};
        std::unique_ptr<zarray_t, decltype(&apriltag_detections_destroy)> ds(apriltag_detector_detect(impl_->detector.get(), &view), apriltag_detections_destroy);
        if (!ds)
            throw std::runtime_error("TAG_BACKEND_FAILURE");
        std::vector<TagObservation> out;
        std::map<int, int> counts;
        for (int i = 0; i < zarray_size(ds.get()); ++i)
        {
            apriltag_detection_t *d = nullptr;
            zarray_get(ds.get(), i, &d);
            if (!impl_->config.allowed_ids_.count(d->id))
                continue;
            TagObservation t;
            t.stamp_ = stamp;
            t.id_ = d->id;
            auto side = impl_->config.side_m_.find(d->id);
            if (side != impl_->config.side_m_.end())
                t.side_m_ = side->second;
            for (size_t p = 0; p < 4; ++p)
                t.corners_[p] = {d->p[p][0], d->p[p][1]};
            if (!detail::valid_quad(t.corners_, bgr.size()))
                continue;
            ++counts[t.id_];
            out.push_back(t);
        }
        out.erase(std::remove_if(out.begin(), out.end(), [&](const auto &t)
        {
            return counts[t.id_] != 1;
        }), out.end());
        return out;
    }

    PoseObservation estimate_exit(const TagObservation &t, cv::Size size, const PoseEstimator &estimator)
    {
        if (!std::isfinite(t.side_m_) || t.side_m_ <= 0)
        {
            PoseObservation p;
            p.stamp_ = t.stamp_;
            p.source_ = "APRILTAG";
            p.target_id_ = "tag36h11:" + std::to_string(t.id_);
            p.reason_ = "METRIC_SIZE_MISSING_OR_INVALID";
            return p;
        }
        double h = t.side_m_ / 2;
        std::array<cv::Point3d, 4> model{{{-h, h, 0}, {h, h, 0}, {h, -h, 0}, {-h, -h, 0}}};
        auto out = estimator.estimate_planar(model, t.corners_, size, t.stamp_, "tag36h11:" + std::to_string(t.id_));
        out.source_ = "APRILTAG";
        return out;
    }

}
