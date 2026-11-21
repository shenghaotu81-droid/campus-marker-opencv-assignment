#include "vision/pose/pose_io.hpp"

namespace mark::pose
{
    // 将 CaptureStamp 写成 YAML map。帧号与接收时间均为十进制字符串。
    void write_capture_stamp(cv::FileStorage &fs, const CaptureStamp &s)
    {
        fs << "{" << "frame_id" << std::to_string(s.frame_id_) << "received_us"
           << std::to_string(s.received_us_);
        fs << "}"; // 关 map
    }

    // YAML map 每条写入 stamp、source、detection_index 和 status。
    // 有名义解时写 estimate，原因非空时写 reason。
    void write_pose_observation(cv::FileStorage &fs, const PoseObservation &p)
    {
        // 开 map + 写 stamp：打开 map，"stamp" key，调 write_capture_stamp 写这条观测的 CaptureStamp
        fs << "{" << "stamp";
        write_capture_stamp(fs, p.stamp_);
        // 基本字段
        // source："MARK" 或 "APRILTAG"，字符串直接写。
        // detection_index：to_string 转字符串写（同前 int64 ）。
        // status：pose_status_name(p)——返回 "ESTIMATED" 或 "UNAVAILABLE"（const char*，因为是字面量）
        fs << "source" << p.source_ << "detection_index"
           << std::to_string(p.detection_index_) << "status" << pose_status_name(p);
        // estimate（可选）：
        // 有值（ESTIMATED）才写 estimate 这个子 map，里面是 rvec、t_m（都用 cv::Mat(...) 包一下写成 !!opencv-matrix）、rmse_px（double 直接写）、translation_spread_m2（S，Mat 包一下）；
        // UNAVAILABLE 的观测没有 estimate，这一项就不写
        if (const auto &m = p.estimate_) // C++17 的 if 带初始化：m 绑到这个 optional 上，条件就是"optional 里有没有值"
            fs << "estimate" << "{" << "rvec" << cv::Mat(m->pose_.rvec_) << "t_m"
               << cv::Mat(m->pose_.t_m_) << "rmse_px" << m->rmse_px_ << "translation_spread_m2"
               << cv::Mat(m->translation_spread_m2_) << "}"; // m-> 这里是 optional 的 operator->——m 本身是 optional，m->pose_ 就是取它里面装的那个 PoseEstimate 再取 pose_
        // reason（可选）
        // 失败原因字符串，非空才写（UNAVAILABLE 的观测才有，比如 "IMAGE_SIZE_OR_QUAD_INVALID"；ESTIMATED 的 reason 是空的，不写）
        if (!p.reason_.empty())
            fs << "reason" << p.reason_;
        fs << "}"; // 关 map
    }

}
