// Tag 检测器的集成测试（A01-A11）
#include "support/g2_test_scene.hpp"
#include "mark/tag_detector.hpp"

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
    try
    {
        auto cfg = g2_test::config();
        auto stamp_ = g2_test::stamp(cfg);
        auto image = fixture();
        cfg.tag_.allowed_ids_ = {0};
        cfg.tag_.min_decision_margin_ = 1; // 明确无噪声fixture阈值，不是官方现场预算。
        mark::pose::TagDetector detector(cfg.tag_);
        auto tags = detector.detect(image, stamp_);
        g2_test::require(tags.size() == 1 && tags[0].id_ == 0 && tags[0].decode_accepted_,
                          "A01 TAG36H11 ID0必须解出");
        g2_test::require(tags[0].corners_[0].x < tags[0].corners_[1].x &&
            tags[0].corners_[0].y > tags[0].corners_[3].y,
            "A02 规范 p0/p1/p2/p3 角序不得误当屏幕序");
        cfg.tag_.allowed_ids_ = {1};
        tags = mark::pose::TagDetector(cfg.tag_).detect(image, stamp_);
        g2_test::require(tags.size() == 1 && !tags[0].decode_accepted_,
                          "A03 非白名单仍留像素证据但不能用于退出");
        cfg.tag_.allowed_ids_ = {0};
        cfg.tag_.min_decision_margin_.reset();
        tags = mark::pose::TagDetector(cfg.tag_).detect(image, stamp_);
        g2_test::require(!tags[0].decode_accepted_,
                          "A04 未标定质量预算不默许解码合格");
        auto exit = mark::pose::estimate_exit(tags[0], image.size(), mark::pose::PoseEstimator(cfg));
        g2_test::require(!exit.pose_.T_camera_target_ && !exit.alignment_measurement_usable_,
                          "A05 未知 Tag 公制尺寸必须拒绝公制对准");
        cv::Mat blank(image.size(), CV_8UC3, cv::Scalar::all(255));
        g2_test::require(detector.detect(blank, stamp_).empty(), "A06 空白图无 Tag");
        cv::Mat two;
        cv::hconcat(image, image, two);
        tags = detector.detect(two, stamp_);
        g2_test::require(tags.size() == 2 && !tags[0].decode_accepted_ && !tags[1].decode_accepted_,
                          "A07 同帧同 ID 多实例不能选择错误目标");
        bool threw = false;
        try { detector.detect(cv::Mat{}, stamp_); }
        catch (const std::invalid_argument &) { threw = true; }
        g2_test::require(threw, "A08 空输入必须明确报错");
        // 非连续 ROI 测试，确保 stride 处理不依赖紧密存储。
        cv::Mat padded(image.rows + 20, image.cols + 20, CV_8UC3, cv::Scalar::all(255));
        auto roi = padded(cv::Rect(10, 10, image.cols, image.rows));
        image.copyTo(roi);
        g2_test::require(!roi.isContinuous() && detector.detect(roi, stamp_).size() == 1,
                          "A09 非连续 Mat 输入应正常检测");
        cfg.tag_.min_decision_margin_ = 1;
        cfg.tag_.side_m_[0] = 0.12; // 仅构造本测试的已知真值，不是现场标签边长。
        const double h = cfg.tag_.side_m_.at(0) / 2;
        const std::vector<cv::Point3d> object{{-h, h, 0}, {h, h, 0},
                                              {h, -h, 0}, {-h, -h, 0}};
        const auto truth = g2_test::truth();
        std::vector<cv::Point2d> projected;
        cv::projectPoints(object, truth.rvec_, truth.t_m_, cfg.camera_.K_, cfg.camera_.dist_, projected);
        mark::pose::TagObservation known;
        known.stamp_ = stamp_;
        known.id_ = 0;
        known.whitelisted_ = true;
        known.decode_accepted_ = true;
        known.decision_margin_ = 10;
        std::copy(projected.begin(), projected.end(), known.corners_.begin());
        exit = mark::pose::estimate_exit(known, cfg.camera_.image_size_, mark::pose::PoseEstimator(cfg));
        g2_test::require(exit.pose_.status_ == mark::pose::PoseStatus::Unique &&
            cv::norm(exit.pose_.T_camera_target_->t_m_ - truth.t_m_) < 1e-6 &&
            !exit.alignment_measurement_usable_, "A10 Tag规范点序与公制位姿接通但不批准实车");
        known.family_ = "tag25h9";
        exit = mark::pose::estimate_exit(known, cfg.camera_.image_size_, mark::pose::PoseEstimator(cfg));
        g2_test::require(!exit.pose_.T_camera_target_, "A11 错 family 不得用于退出位姿");
        std::cout << "G2 tag A01-A11 PASS\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}