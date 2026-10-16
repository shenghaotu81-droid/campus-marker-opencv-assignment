// 理想场景/主动断言
// 造测试用的 G2Config 和时间戳。注意 18-20 行写死的 0.05/0.1/1——测试专用理想阈值，严禁抄进生产 pose.yaml
// 给测试喂数据的，别跟生产配置搞混（两个测试（pose_contract_test、tag_detector_test））
#pragma once

#include "mark/pose_estimator.hpp"
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <vector>
#include <opencv2/calib3d.hpp>

namespace g2_test
{
// 测试专属理想成像预算，只验证算法/契约；严禁复制到生产 pose.yaml。
inline mark::pose::G2Config config()
{
    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    auto c = mark::pose::load_pose_config(root / "config/pose.yaml");
    c.budgets_.max_rmse_px_ = 0.05;
    c.budgets_.max_corner_error_px_ = 0.1;
    c.budgets_.min_quad_area_px2_ = 1;
    return c;
}

inline mark::pose::CaptureStamp stamp(const mark::pose::G2Config &c)
{
    mark::pose::CaptureStamp s;
    s.session_id_ = "synthetic-test";
    s.camera_id_ = c.camera_.camera_id_;
    s.clock_domain_ = "synthetic-clock";
    s.pixel_domain_ = mark::pose::PixelDomain::RawDistorted;
    s.frame_id_ = 10;
    s.received_us_ = 1000000;
    s.exposure_us_ = 999000;
    s.time_uncertainty_s_ = 0;
    return s;
}

inline mark::pose::RigidPose truth()
{
    return {cv::Vec3d(0.4, -0.3, 0.2), cv::Vec3d(-0.04, -0.04, 0.55)};
}

// 从明确真值投影，不通过被测求解器生成目标位姿。
inline std::array<cv::Point2d, 4> pixels(const mark::pose::G2Config &c)
{
    const auto p = truth();
    std::vector<cv::Point2d> projected;
    cv::projectPoints(std::vector<cv::Point3d>(c.mark_points_m_.begin(), c.mark_points_m_.end()),
                      p.rvec_, p.t_m_, c.camera_.K_, c.camera_.dist_, projected);
    std::array<cv::Point2d, 4> out;
    std::copy(projected.begin(), projected.end(), out.begin());
    return out;
}

inline mark::FrameResult raw(const mark::pose::G2Config &c)
{
    const auto s = stamp(c);
    mark::FrameResult out{};
    out.frame_id = s.frame_id_;
    out.timestamp_us = *s.exposure_us_;
    out.status = mark::Status::DETECTED;
    mark::Detection d{};
    const auto p = pixels(c);
    for (std::size_t i = 0; i < p.size(); ++i)
        d.corners[i] = p[i];
    d.attributes.orientation = std::array<int, 4>{{0, 1, 2, 3}};
    out.detections.push_back(d);
    return out;
}

// 不使用 assert：Release 下测试也必须真的失败，不能因 NDEBUG 自动变绿。
inline void require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
} // namespace g2_test