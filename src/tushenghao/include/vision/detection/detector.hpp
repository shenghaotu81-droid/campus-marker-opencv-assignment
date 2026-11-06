#pragma once
#include <memory>
#include "vision/config/detector_config.hpp"
#include "vision/types/detector_types.hpp"
namespace mark
{
    class Detector
    {
public:
        explicit Detector(DetectorConfig);
        ~Detector();
        Detector(const Detector &) = delete;
        Detector& operator=(const Detector &) = delete;
        FrameResult process(const FrameInput &);
        void reset() noexcept;
private: struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
