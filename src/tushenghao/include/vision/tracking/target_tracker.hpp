#pragma once
#include "vision/config/tracking_config.hpp"
#include <memory>
namespace mark::tracking
{
    class TargetTracker
    {
public:
        explicit TargetTracker(TrackingConfig);
        ~TargetTracker();
        TargetTracker(const TargetTracker &) = delete;
        TargetTracker& operator=(const TargetTracker &) = delete;
        TrackingResult step(const TrackingFrame &);
        void reset() noexcept;
private: struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
