#pragma once
#include "vision/pose/pose_estimator.hpp"
#include <opencv2/calib3d.hpp>
#include <filesystem>
#include <stdexcept>
namespace pose_test
{
    inline void require(bool b, const char *m)
    {
        if (!b)
            throw std::runtime_error(m);
    }

    inline mark::pose::PoseConfig config()
    {
        return mark::pose::load_pose_config(std::filesystem::path(VISION_SOURCE_ROOT) / "config/pose.yaml");
    }

    inline mark::pose::CaptureStamp stamp(const mark::pose::PoseConfig &c)
    {
        mark::pose::CaptureStamp s;
        s.session_id_ = "fixture";
        s.camera_id_ = c.camera_.camera_id_;
        s.clock_domain_ = "fixture";
        s.pixel_domain_ = mark::pose::PixelDomain::RawDistorted;
        s.frame_id_ = 1;
        s.received_us_ = 0;
        return s;
    }

    inline mark::pose::RigidPose truth()
    {
        return {{.15, -.1, .05}, {-.04, -.04, .55}};
    }

    inline std::array<cv::Point2d, 4> pixels(const mark::pose::PoseConfig &c)
    {
        std::vector<cv::Point2d> p;
        auto t = truth();
        cv::projectPoints(std::vector<cv::Point3d>(c.mark_points_m_.begin(), c.mark_points_m_.end()), t.rvec_, t.t_m_, c.camera_.K_, c.camera_.dist_, p);
        std::array<cv::Point2d, 4> out;
        std::copy(p.begin(), p.end(), out.begin());
        return out;
    }

}
