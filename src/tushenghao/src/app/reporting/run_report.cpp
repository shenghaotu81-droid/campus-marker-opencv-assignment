// 汇总帧数、检测、位姿、跟踪和原因计数，写入本次运行的 summary.yaml。
// 写 summary.yaml 的地方
#include "reporting/run_report.hpp"

namespace mark
{
    void RunSummary::add(const detector::FrameResult &frame)
    {
        ++frames_;
        detections_ += frame.detections_.size();
        static const char *names[]{"INVALID_INPUT", "NOT_DETECTED", "DETECTED"};
        ++statuses_[names[static_cast<size_t>(frame.status_)]];
        for (const auto &reason_ : frame.diagnostics_)
            ++reasons_[reason_];
    }

    void write_run_summary(const RunSummary &s, const std::string &path)
    {
        cv::FileStorage fs(path, cv::FileStorage::WRITE);
        fs << "frames" << std::to_string(s.frames_) << "detections" << std::to_string(s.detections_);
        for (const auto &entry : {std::make_pair("statuses", &s.statuses_),
                                  {"reasons", &s.reasons_},
                                  {"poses", &s.poses_},
                                  {"tracks", &s.tracks_}})
        {
            fs << entry.first << "[";
            for (const auto &[name, count] : *entry.second)
                fs << "{" << "name" << name << "count" << std::to_string(count) << "}";
            fs << "]";
        }
        fs << "elapsed_s" << s.elapsed_s_;
    }

}
