// 离线验证工具自己的最小schema-5 reader，不链接进任何生产库。
#include "vision/tracking/target_tracker.hpp"
#include "io/tracking_io.hpp"
#include <fstream>
#include <iostream>
static int64_t integer(const cv::FileNode &n)
{
    return n.isString() ? std::stoll(n.string()) : int64_t(n.real());
}

static mark::pose::CaptureStamp stamp(const cv::FileNode &n)
{
    mark::pose::CaptureStamp s;
    s.session_id_ = n["session_id"].string();
    s.camera_id_ = n["camera_id"].string();
    s.clock_domain_ = n["clock_domain"].string();
    s.pixel_domain_ = mark::pose::PixelDomain::RawDistorted;
    s.frame_id_ = integer(n["frame_id"]);
    s.received_us_ = integer(n["received_us"]);
    if (n["exposure_us"].isString() && !n["exposure_us"].string().empty())
        s.exposure_us_ = integer(n["exposure_us"]);
    if (n["time_uncertainty_s"].isReal() || n["time_uncertainty_s"].isInt())
        s.time_uncertainty_s_ = n["time_uncertainty_s"].real();
    return s;
}

int main(int argc, char **argv)
{
    try{
        if (argc != 4)
            return 1;
        cv::FileStorage input(argv[1], cv::FileStorage::READ), out(argv[3], cv::FileStorage::WRITE);
        if (!input.isOpened() || !out.isOpened() || int(input["schema_version"]) != 5)
            return 1;
        mark::tracking::TargetTracker tracker(mark::tracking::load_tracking_config(argv[2]));
        out << "schema_version" << 2 << "stream_type" << "G4TrackingResult" << "frames" << "[";
        for (const auto &node:input["frames"])
        {
            mark::tracking::TrackingFrame f;
            f.stamp_ = stamp(node["stamp"]);
            for (const auto &p:node["observations"])
            {
                mark::pose::PoseObservation o;
                o.stamp_ = stamp(p["stamp"]);
                o.source_ = p["source"].string();
                o.target_id_ = p["target_id"].string();
                o.detection_index_ = integer(p["detection_index"]);
                auto status = p["status"].string();
                o.status_ = status == "UNIQUE" ? mark::pose::PoseStatus::Unique : status == "AMBIGUOUS" ? mark::pose::PoseStatus::Ambiguous : mark::pose::PoseStatus::NotDetected;
                for (const auto &c:p["candidates"])
                {
                    mark::pose::PoseCandidate candidate;
                    candidate.rmse_px_ = c["rmse_px"].real();
                    cv::Mat r, t;
                    c["rvec"] >> r;
                    c["t_m"] >> t;
                    for (int i = 0; i < 3; ++i)
                    {
                        candidate.pose_.rvec_[i] = r.at<double>(i);
                        candidate.pose_.t_m_[i] = t.at<double>(i);
                    }
                    o.candidates_.push_back(candidate);
                }
                if (!p["selected_index"].empty())
                    o.selected_index_ = size_t(integer(p["selected_index"]));
                f.observations_.push_back(o);
            }
            auto result = tracker.step(f);
            if (!result.input_accepted_)
                return 1;
            mark::tracking::write_tracking_result(out, result);
        }
        out << "]";
        return 0;
    }
    catch (const std::exception &e){
        std::cerr << e.what() << '\n';
        return 1;
    }
}
