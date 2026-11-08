// Tag 检测器的集成测试（）
#include "pose_test_scene.hpp"
#include "vision/pose/tag_detector.hpp"

#include <iostream>
#include <opencv2/imgproc.hpp>
#include <apriltag/apriltag.h>
#include <apriltag/tag36h11.h>

// 官方库生成编码图供集成测试；这只证明同库接口一致性，不冒充独立真实 Tag 泛化。
cv::Mat fixture()
{
    std::unique_ptr<apriltag_family_t, decltype(&tag36h11_destroy)> family_(
        tag36h11_create(), tag36h11_destroy);
    auto *raw = apriltag_to_image(family_.get(), 0);
    if (!raw)
        throw std::runtime_error("生成测试 Tag 失败");
    cv::Mat gray(raw->height, raw->width, CV_8U, raw->buf, raw->stride);
    cv::Mat tile;
    cv::resize(gray, tile, {}, 20, 20, cv::INTER_NEAREST);
    // Debian/Ubuntu 的图像辅助函数由独立 utils 链接；fixture 先复制像素再释放。
    image_u8_destroy(raw);
    cv::copyMakeBorder(tile, tile, 80, 80, 80, 80, cv::BORDER_CONSTANT, cv::Scalar(255));
    cv::Mat bgr;
    cv::cvtColor(tile, bgr, cv::COLOR_GRAY2BGR);
    return bgr;
}

int main()
{
    try {
        auto cfg = pose_test::config();
        auto s = pose_test::stamp(cfg);
        auto image_ = fixture();
        cfg.tag_.allowed_ids_ = {0};
        cfg.tag_.side_m_[0] = .12;
        mark::pose::TagDetector detector(cfg.tag_);
        auto tags = detector.detect(image_, s);
        pose_test::require(tags.size() == 1 && tags[0].id_ == 0, "approved tag missing");
        pose_test::require(tags[0].corners_[0].x<tags[0].corners_[1].x && tags[0].corners_[0].y>tags[0].corners_[3].y, "tag corner convention changed");
        auto unknown = cfg.tag_;
        unknown.side_m_.clear();
        auto pixels = mark::pose::TagDetector(unknown).detect(image_, s);
        pose_test::require(pixels.size() == 1 && !mark::pose::estimate_exit(pixels[0], image_.size(), mark::pose::PoseEstimator(cfg)).estimate_, "unknown metric size blocked pixel decode or invented pose");
        auto denied = cfg.tag_;
        denied.allowed_ids_ = {1};
        denied.side_m_.clear();
        denied.side_m_[1] = .12;
        pose_test::require(mark::pose::TagDetector(denied).detect(image_, s).empty(), "wrong ID accepted");
        cv::Mat two;
        cv::hconcat(image_, image_, two);
        pose_test::require(detector.detect(two, s).empty(), "duplicate identity selected");
        cv::Mat blank(image_.size(), CV_8UC3, cv::Scalar::all(255));
        pose_test::require(detector.detect(blank, s).empty(), "blank tag");
        cv::Mat padded(image_.rows + 20, image_.cols + 20, CV_8UC3, cv::Scalar::all(255));
        auto roi = padded(cv::Rect(10, 10, image_.cols, image_.rows));
        image_.copyTo(roi);
        pose_test::require(detector.detect(roi, s).size() == 1, "non-contiguous stride broken");
        bool failed = false;
        try{
            detector.detect({}, s);
        }
        catch (const std::invalid_argument &){
            failed = true;
        }
        pose_test::require(failed, "empty C input");
        return 0;
    }
    catch (const std::exception &e){
        std::cerr << e.what() << '\n';
        return 1;
    }
}
