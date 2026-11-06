// 离线验证工具自己的最小schema-6 reader，不链接进任何生产库。
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
        if (!input.isOpened() || !out.isOpened() || int(input["schema_version"]) != 6)
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
                const auto status = p["status"].string();
                if (status == "ESTIMATED")
                {
                    o.estimate_.emplace();
                    const auto m = p["estimate"];
                    cv::Mat r, t, spread;
                    m["rvec"] >> r; m["t_m"] >> t; m["translation_spread_m2"] >> spread;
                    if (r.type() != CV_64F || r.total() != 3 || t.type() != CV_64F || t.total() != 3 ||
                        spread.type() != CV_64F || spread.rows != 3 || spread.cols != 3) return 1;
                    r.copyTo(cv::Mat(o.estimate_->pose_.rvec_, false));
                    t.copyTo(cv::Mat(o.estimate_->pose_.t_m_, false));
                    spread.copyTo(cv::Mat(o.estimate_->translation_spread_m2_, false));
                    o.estimate_->rmse_px_ = m["rmse_px"].real();
                }
                else if (status != "UNAVAILABLE") return 1;
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
