// Tag 检测器的集成测试（A01-A11）
#include "g2_test_scene.hpp"
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
        auto cfg = g2_test::config();
        auto s = g2_test::stamp(cfg);
        auto image = fixture();
        cfg.tag_.allowed_ids_ = {0};
        cfg.tag_.side_m_[0] = .12;
        mark::pose::TagDetector detector(cfg.tag_);
        auto tags = detector.detect(image, s);
        g2_test::require(tags.size() == 1 && tags[0].id_ == 0, "approved tag missing");
        g2_test::require(tags[0].corners_[0].x<tags[0].corners_[1].x && tags[0].corners_[0].y>tags[0].corners_[3].y, "tag corner convention changed");
        auto unknown = cfg.tag_;
        unknown.side_m_.clear();
        auto pixels = mark::pose::TagDetector(unknown).detect(image, s);
        g2_test::require(pixels.size() == 1 && !mark::pose::estimate_exit(pixels[0], image.size(), mark::pose::PoseEstimator(cfg)).estimate_, "unknown metric size blocked pixel decode or invented pose");
        auto denied = cfg.tag_;
        denied.allowed_ids_ = {1};
        denied.side_m_.clear();
        denied.side_m_[1] = .12;
        g2_test::require(mark::pose::TagDetector(denied).detect(image, s).empty(), "wrong ID accepted");
        cv::Mat two;
        cv::hconcat(image, image, two);
        g2_test::require(detector.detect(two, s).empty(), "duplicate identity selected");
        cv::Mat blank(image.size(), CV_8UC3, cv::Scalar::all(255));
        g2_test::require(detector.detect(blank, s).empty(), "blank tag");
        cv::Mat padded(image.rows + 20, image.cols + 20, CV_8UC3, cv::Scalar::all(255));
        auto roi = padded(cv::Rect(10, 10, image.cols, image.rows));
        image.copyTo(roi);
        g2_test::require(detector.detect(roi, s).size() == 1, "non-contiguous stride broken");
        bool failed = false;
        try{
            detector.detect({}, s);
        }
        catch (const std::invalid_argument &){
            failed = true;
        }
        g2_test::require(failed, "empty C input");
        auto bad = cfg.tag_;
        bad.side_m_[0] = 0;
        failed = false;
        try{
            mark::pose::TagDetector d(bad);
        }
        catch (const std::exception &){
            failed = true;
        }
        g2_test::require(failed, "bad metric size accepted");
        return 0;
    }
    catch (const std::exception &e){
        std::cerr << e.what() << '\n';
        return 1;
    }
}
