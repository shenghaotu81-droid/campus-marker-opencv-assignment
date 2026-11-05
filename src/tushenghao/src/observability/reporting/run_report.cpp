#include "reporting/run_report.hpp"
namespace mark {
void RunSummary::add(const FrameResult &frame) {
    ++frames;
    detections += frame.detections.size();
    static const char *names[]{"NOT_READY", "INVALID_INPUT", "NOT_DETECTED", "DETECTED"};
    ++statuses[names[static_cast<size_t>(frame.status)]];
    for (const auto &reason : frame.diagnostics) ++reasons[reason];
}
void writeRunSummary(const RunSummary &s, const std::string &path) {
    cv::FileStorage fs(path, cv::FileStorage::WRITE);
    if (!fs.isOpened()) throw std::runtime_error("SUMMARY_WRITE_FAILED");
    fs << "frames" << std::to_string(s.frames) << "detections" << std::to_string(s.detections);
    for (const auto &entry : {std::make_pair("statuses", &s.statuses), {"reasons", &s.reasons},
                              {"poses", &s.poses}, {"tracks", &s.tracks}}) {
        fs << entry.first << "[";
        for (const auto &[name, count] : *entry.second)
            fs << "{" << "name" << name << "count" << std::to_string(count) << "}";
        fs << "]";
    }
    if (s.elapsed_s) fs << "elapsed_s" << *s.elapsed_s;
}
}
