#pragma once

#include "mark/tracking_config.hpp"
#include <memory>

namespace mark::tracking
{
// 单实例顺序调用；不修改G2，不回灌PosePrior，不输出控制指令。
class TargetTracker
{
public:
    explicit TargetTracker(TrackingConfig config);
    ~TargetTracker();
    TargetTracker(const TargetTracker &) = delete;
    TargetTracker &operator=(const TargetTracker &) = delete;

    TrackingResult step(const TrackingFrame &frame);
    TargetForecast predict_to(std::int64_t target_time_us) const;
    void reset() noexcept;
    const TrackingConfig &config() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace mark::tracking
