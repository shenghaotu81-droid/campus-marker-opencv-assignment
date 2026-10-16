// AprilTag 检测器的 C++ 封装,对外给 C++ 接口(C 和 C++ 混编，RAII 管资源)
// C库生命周期/解码/Tag位姿适配
#include "mark/tag_detector.hpp"
#include "pose/pose_geometry.hpp"

#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <opencv2/imgproc.hpp>
#include <apriltag/apriltag.h>
#include <apriltag/tag36h11.h>

namespace mark::pose
{
// Impl 结构体：用 unique_ptr + 自定义 deleter 管 apriltag_detector_t 和 family_t 的创建/销毁，防泄漏(C 对象)
// 原项目没有 Tag 路径；每实例独占检测器和 family，按 detector->family 顺序释放。
struct TagDetector::Impl
{
    TagConfig config_;
    std::unique_ptr<apriltag_family_t, decltype(&tag36h11_destroy)> family_{
        tag36h11_create(), tag36h11_destroy};
    std::unique_ptr<apriltag_detector_t, decltype(&apriltag_detector_destroy)> detector_{
        apriltag_detector_create(), apriltag_detector_destroy};

    explicit Impl(TagConfig config) : config_(std::move(config))
    {
        if (!family_ || !detector_)
            throw std::runtime_error("AprilTag 对象创建失败");
        const auto &c = config_;
        if (c.nthreads_ < 1 || !std::isfinite(c.quad_decimate_) || c.quad_decimate_ < 1 ||
            !std::isfinite(c.quad_sigma_) || c.quad_sigma_ < 0 ||
            !std::isfinite(c.decode_sharpening_) || c.decode_sharpening_ < 0 ||
            c.max_hamming_ < 0 || c.max_hamming_ > 2 ||
            (c.min_decision_margin_ && (!std::isfinite(*c.min_decision_margin_) ||
                                       *c.min_decision_margin_ <= 0)))
            throw std::runtime_error("AprilTag 参数无效");
        for (int id_ : c.allowed_ids_)
            if (id_ < 0 || static_cast<std::uint32_t>(id_) >= family_->ncodes)
                throw std::runtime_error("AprilTag ID 越界");
        detector_->nthreads = c.nthreads_;
        detector_->quad_decimate = static_cast<float>(c.quad_decimate_);
        detector_->quad_sigma = static_cast<float>(c.quad_sigma_);
        detector_->refine_edges = c.refine_edges_;
        detector_->decode_sharpening = c.decode_sharpening_;
        // 显式指定允许纠错数；不能调用默认允许两位纠错的便捷函数。
        apriltag_detector_add_family_bits(detector_.get(), family_.get(), c.max_hamming_);
    }
};

// 构造函数：验参数（线程数、decimate 等），把配置写进 C 结构体
TagDetector::TagDetector(TagConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}
TagDetector::~TagDetector() = default;

// detect + estimate_exit：detect 调 C 库找 Tag，estimate_exit 拿角点调 estimate_planar 算位姿（跟 MARK 一样）

// 复制检测值后立即释放 C 检测结果；不把生命周期短的 C 指针交给调用者。
std::vector<TagObservation> TagDetector::detect(const cv::Mat &bgr, const CaptureStamp &stamp_)
{
    if (bgr.empty() || bgr.type() != CV_8UC3 || !detail::valid_stamp(stamp_))
        throw std::invalid_argument("Tag 输入必须为有效 BGR8 原图及来源时间");
    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    if (gray.step > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("Tag 图像步长超出 C API 范围");
    // C++17 聚合初始化，真实 stride 使用 Mat.step，不假设一定等于 width。
    image_u8_t view{gray.cols, gray.rows, static_cast<int>(gray.step), gray.data};
    std::unique_ptr<zarray_t, decltype(&apriltag_detections_destroy)> detections(
        apriltag_detector_detect(impl_->detector_.get(), &view), apriltag_detections_destroy);
    if (!detections)
        throw std::runtime_error("AprilTag 检测返回空句柄");
    std::vector<TagObservation> result;
    std::map<int, int> id_count;
    for (int i = 0; i < zarray_size(detections.get()); ++i)
    {
        apriltag_detection_t *d = nullptr;
        zarray_get(detections.get(), i, &d);
        TagObservation t;
        t.stamp_ = stamp_;
        t.id_ = d->id;
        t.hamming_ = d->hamming;
        t.decision_margin_ = d->decision_margin;
        for (std::size_t p = 0; p < t.corners_.size(); ++p)
            t.corners_[p] = {d->p[p][0], d->p[p][1]};
        const auto &c = impl_->config_;
        t.whitelisted_ = c.allowed_ids_.count(t.id_) != 0;
        if (!t.whitelisted_)
            t.reasons_.push_back("TAG_ID_NOT_APPROVED");
        if (!c.min_decision_margin_)
            t.reasons_.push_back("TAG_MARGIN_BUDGET_MISSING");
        else if (!std::isfinite(t.decision_margin_) || t.decision_margin_ < *c.min_decision_margin_)
            t.reasons_.push_back("TAG_MARGIN_REJECTED");
        if (t.hamming_ > c.max_hamming_ || !detail::valid_quad(t.corners_, bgr.size()))
            t.reasons_.push_back("TAG_DECODE_OR_QUAD_REJECTED");
        t.decode_accepted_ = t.reasons_.empty();
        ++id_count[t.id_];
        result.push_back(std::move(t));
    }
    for (auto &t : result)
        if (id_count[t.id_] != 1)
        {
            t.decode_accepted_ = false;
            t.reasons_.push_back("DUPLICATE_TAG_ID_IN_FRAME");
        }
    return result;
}

// Tag 边长使用黑白检测边界；物理角序严格对应 apriltag.h 的 H/p 定义，不按屏幕重排。
ExitObservation estimate_exit(const TagObservation &tag_, cv::Size size,
                              const PoseEstimator &estimator)
{
    ExitObservation result;
    result.tag_ = tag_;
    result.pose_.stamp_ = tag_.stamp_;
    result.pose_.source_ = "APRILTAG";
    result.pose_.target_id_ = "tag36h11:" + std::to_string(tag_.id_);
    const auto &c = estimator.config().tag_;
    const auto side = c.side_m_.find(tag_.id_);
    if (tag_.family_ != "tag36h11" || !tag_.decode_accepted_ || !tag_.whitelisted_ ||
        !c.allowed_ids_.count(tag_.id_) || !c.min_decision_margin_ || tag_.hamming_ < 0 ||
        tag_.hamming_ > c.max_hamming_ || !std::isfinite(tag_.decision_margin_) ||
        tag_.decision_margin_ < *c.min_decision_margin_ || side == c.side_m_.end())
    {
        result.pose_.status_ = PoseStatus::BudgetMissing;
        result.pose_.reasons_.push_back("TAG_ID_QUALITY_OR_METRIC_SIZE_UNAVAILABLE");
        return result;
    }
    const double h = side->second / 2;
    const std::array<cv::Point3d, 4> object{{{-h, h, 0}, {h, h, 0},
                                           {h, -h, 0}, {-h, -h, 0}}};
    result.pose_ = estimator.estimate_planar(object, tag_.corners_, size, tag_.stamp_,
                                           result.pose_.target_id_);
    result.pose_.source_ = "APRILTAG";
    const auto &cfg = estimator.config();
    result.pose_.control_usable_ = result.pose_.status_ == PoseStatus::Unique &&
        cfg.camera_.hardware_verified_ && cfg.budgets_.hardware_verified_ &&
        cfg.selection_.constraints_verified_ && c.hardware_verified_ &&
        cfg.budgets_.max_time_uncertainty_s_ && tag_.stamp_.exposure_us_ &&
        tag_.stamp_.time_uncertainty_s_ &&
        *tag_.stamp_.time_uncertainty_s_ <= *cfg.budgets_.max_time_uncertainty_s_;
    if (!result.pose_.control_usable_)
        result.pose_.reasons_.push_back("TAG_MEASUREMENT_NOT_HARDWARE_QUALIFIED");
    result.alignment_measurement_usable_ = result.pose_.control_usable_;
    return result;
}
} // namespace mark::pose