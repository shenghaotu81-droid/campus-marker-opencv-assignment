#include "vision/pose/pose_io.hpp"

namespace mark::pose
{
    // 把一个 CaptureStamp 写成一个 YAML map，字段有值就写值，没值（optional 为空）就写空字符串 ""
    void write_capture_stamp(cv::FileStorage &fs, const CaptureStamp &s)
    {
        // 开 map + 写字段
        /*
        1. session_id：s.session_id_，字符串，直接写。
        2. camera_id：s.camera_id_，字符串，直接写。
        3. clock_domain：s.clock_domain_，字符串，直接写。
        4. pixel_domain：int(s.pixel_domain_)——这是个 enum，强转成 int 再写（0/1 这样的数字）。
        5. frame_id：std::to_string(s.frame_id_)，int64 转字符串。
        6. received_us：std::to_string(s.received_us_)，int64 转字符串。
        7. exposure_us：optional，有值 to_string，没值写 ""。
        8. time_uncertainty_s：optional，有值写 double，没值写 ""。
        */
        fs << "{" << "session_id" << s.session_id_ << "camera_id" << s.camera_id_ << "clock_domain"
           << s.clock_domain_ << "pixel_domain" << int(s.pixel_domain_) << "frame_id"
           << std::to_string(s.frame_id_) << "received_us" << std::to_string(s.received_us_)
           << "exposure_us" << (s.exposure_us_ ? std::to_string(*s.exposure_us_) : "") // 三元运算符
           << "time_uncertainty_s";
        if (s.time_uncertainty_s_)
            fs << *s.time_uncertainty_s_;
        else
            fs << "";
        fs << "}"; // 关 map
    }

    // 把一条 PoseObservation 写成一个 YAML map——stamp、source、target_id、detection_index、status 是每条都有；estimate（位姿估计）只有 ESTIMATED 的才有；reason（失败原因）只有非空才写
    void write_pose_observation(cv::FileStorage &fs, const PoseObservation &p)
    {
        // 开 map + 写 stamp：打开 map，"stamp" key，调 write_capture_stamp 写这条观测的 CaptureStamp
        fs << "{" << "stamp";
        write_capture_stamp(fs, p.stamp_);
        // 基本字段
        // source："MARK" 或 "APRILTAG"，字符串直接写。
        // target_id："mark-white-structure-v1" 那个，字符串直接写。
        // detection_index：to_string 转字符串写（同前 int64 ）。
        // status：pose_status_name(p)——返回 "ESTIMATED" 或 "UNAVAILABLE"（const char*，因为是字面量）
        fs << "source" << p.source_ << "target_id" << p.target_id_ << "detection_index"
           << std::to_string(p.detection_index_) << "status" << pose_status_name(p);
        // estimate（可选）：
        // 有值（ESTIMATED）才写 estimate 这个子 map，里面是 rvec、t_m（都用 cv::Mat(...) 包一下写成 !!opencv-matrix）、rmse_px（double 直接写）、translation_spread_m2（S，Mat 包一下）；
        // UNAVAILABLE 的观测没有 estimate，这一项就不写
        if (const auto &m = p.estimate_) // C++17 的 if 带初始化：m 绑到这个 optional 上，条件就是"optional 里有没有值"
            fs << "estimate" << "{" << "rvec" << cv::Mat(m->pose_.rvec_) << "t_m"
               << cv::Mat(m->pose_.t_m_) << "rmse_px" << m->rmse_px_ << "translation_spread_m2"
               << cv::Mat(m->translation_spread_m2_) << "}"; // m-> 这里是 optional 的 operator->——m 本身是 optional，m->pose_ 就是取它里面装的那个 PoseEstimate 再取 pose_
        // reason（可选）
        // 失败原因字符串，非空才写（UNAVAILABLE 的观测才有，比如 "STAMP_CAMERA_OR_QUAD_INVALID"；ESTIMATED 的 reason 是空的，不写）
        if (!p.reason_.empty())
            fs << "reason" << p.reason_;
        fs << "}"; // 关 map
    }

}
