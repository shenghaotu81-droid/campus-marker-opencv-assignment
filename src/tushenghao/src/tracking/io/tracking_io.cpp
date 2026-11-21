// 写出逐帧跟踪结果。
#include "io/tracking_io.hpp"
#include "vision/pose/pose_io.hpp"

namespace mark::tracking
{
    void write_tracking_result(cv::FileStorage &fs, const TrackingResult &r)
    {
        fs << "{" << "stamp";
        // 输入帧戳的帧号和微秒时间写成十进制字符串。
        pose::write_capture_stamp(fs, r.input_stamp_);

        fs << "input_accepted" << int(r.input_accepted_) << "events" << "[";
        for (const auto &e : r.events_)
            fs << e;
        fs << "]";

        // 仅有关联分数时写 NIS。
        if (r.association_nis_)
            fs << "association_nis" << *r.association_nis_;

        fs << "tracks" << "[";
        for (const auto &t : r.tracks_)
        {
            fs << "{" << "state"
               << track_state_name(t.state_) << "state_time_us" << std::to_string(t.state_time_us_)
               << "last_measurement_stamp";
            // 最近实测时间戳同样写成字符串，如 frame_id: "42"、received_us: "1700000000000000"。
            pose::write_capture_stamp(fs, t.last_measurement_stamp_);
            fs << "position_m" << cv::Mat(t.position_m_);
            if (t.velocity_mps_)
                fs << "velocity_mps" << cv::Mat(*t.velocity_mps_);
            fs << "covariance" << cv::Mat(t.covariance_);
            fs << "}";
        }
        fs << "]" << "}";
    }
}
