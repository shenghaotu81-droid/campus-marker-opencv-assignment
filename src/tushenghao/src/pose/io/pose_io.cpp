#include "vision/pose/pose_io.hpp"
namespace mark::pose
{
    void write_capture_stamp(cv::FileStorage &fs, const CaptureStamp &s)
    {
        fs << "{" << "session_id" << s.session_id_ << "camera_id" << s.camera_id_ << "clock_domain" << s.clock_domain_ << "pixel_domain" << int(s.pixel_domain_) << "frame_id" << std::to_string(s.frame_id_) << "received_us" << std::to_string(s.received_us_) << "exposure_us" << (s.exposure_us_ ? std::to_string(*s.exposure_us_) : "") << "time_uncertainty_s";
        if (s.time_uncertainty_s_)
            fs << *s.time_uncertainty_s_;
        else
            fs << "";
        fs << "}";
    }

    void write_pose_observation(cv::FileStorage &fs, const PoseObservation &p)
    {
        fs << "{" << "stamp";
        write_capture_stamp(fs, p.stamp_);
        fs << "source" << p.source_ << "target_id" << p.target_id_
           << "detection_index" << std::to_string(p.detection_index_)
           << "status" << pose_status_name(p);
        if (const auto &m = p.estimate_)
            fs << "estimate" << "{" << "rvec" << cv::Mat(m->pose_.rvec_)
               << "t_m" << cv::Mat(m->pose_.t_m_) << "rmse_px" << m->rmse_px_
               << "translation_spread_m2" << cv::Mat(m->translation_spread_m2_) << "}";
        if (!p.reason_.empty())
            fs << "reason" << p.reason_;
        fs << "}";
    }

}
