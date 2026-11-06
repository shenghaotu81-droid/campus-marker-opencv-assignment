#include "io/tracking_io.hpp"
#include "vision/pose/pose_io.hpp"
namespace mark::tracking
{
    void write_tracking_result(cv::FileStorage &fs, const TrackingResult &r)
    {
        fs << "{" << "stamp";
        pose::write_capture_stamp(fs, r.input_stamp_);
        fs << "input_accepted" << int(r.input_accepted_) << "events" << "[";
        for (const auto &e:r.events_)
            fs << e;
        fs << "]";
        if (r.association_nis_)
            fs << "association_nis" << *r.association_nis_;
        fs << "tracks" << "[";
        for (const auto &t:r.tracks_)
        {
            fs << "{" << "track_id" << std::to_string(t.track_id_) << "state" << track_state_name(t.state_) << "state_time_us" << std::to_string(t.state_time_us_) << "last_measurement_stamp";
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
