#pragma once
#include "vision/types/detector_types.hpp"
#include <map>
namespace mark
{
    struct RunSummary
    {
        uint64_t frames{}, detections{};
        std::map<std::string, uint64_t> statuses, reasons, poses, tracks;
        double elapsed_s{};
        void add(const FrameResult &frame);
    };
    void writeRunSummary(const RunSummary &summary, const std::string &path);
}
