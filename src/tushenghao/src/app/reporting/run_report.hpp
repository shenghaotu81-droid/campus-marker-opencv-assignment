#pragma once
#include "vision/types/detector_types.hpp"
#include <map>

namespace mark
{
    struct RunSummary
    {
        uint64_t frames_{}, detections_{};
        std::map<std::string, uint64_t> statuses_, reasons_, poses_, tracks_;
        double elapsed_s_{};
        void add(const detector::FrameResult &frame);
    };

    void write_run_summary(const RunSummary &summary, const std::string &path);
}
