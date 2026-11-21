// 将帧戳和位姿观测写成 YAML map。
#include "vision/pose/pose_io.hpp"

namespace mark::pose
{
    // 帧号和微秒时间均写成十进制字符串。
    void write_capture_stamp(cv::FileStorage &fs, const CaptureStamp &s)
    {
        fs << "{" << "frame_id" << std::to_string(s.frame_id_) << "received_us"
           << std::to_string(s.received_us_);
        fs << "}";
    }

    void write_pose_observation(cv::FileStorage &fs, const PoseObservation &p)
    {
        fs << "{" << "stamp";
        write_capture_stamp(fs, p.stamp_);

        // 离线观测来自 MARK 或 APRILTAG，直接调用通用求解时来源为 PLANAR。
        fs << "source" << p.source_ << "detection_index"
           << std::to_string(p.detection_index_) << "status" << pose_status_name(p);

        // 在条件中绑定估计引用，有值才写名义位姿。
        if (const auto &m = p.estimate_)
            fs << "estimate" << "{" << "rvec" << cv::Mat(m->pose_.rvec_) << "t_m"
               << cv::Mat(m->pose_.t_m_) << "rmse_px" << m->rmse_px_ << "translation_spread_m2"
               << cv::Mat(m->translation_spread_m2_) << "}";

        if (!p.reason_.empty())
            fs << "reason" << p.reason_;
        fs << "}";
    }
}
