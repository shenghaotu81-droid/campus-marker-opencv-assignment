// 校验图像与帧序，运行检测并发布当前帧结果。
#include "vision/detection/detector.hpp"
#include <optional>

#include "pipeline/decode_stage.hpp"

namespace mark::detector
{
    namespace
    {
        struct FrameStamp
        {
            uint64_t frame_id_{};
            int64_t timestamp_us_{};
        };
    }

    struct Detector::Impl
    {
        DetectorConfig config_;

        std::optional<FrameStamp> last_input_;
    };

    Detector::Detector(DetectorConfig config_) : impl_(std::make_unique<Impl>())
    {
        impl_->config_ = std::move(config_);
    }

    Detector::~Detector() = default;

    FrameResult Detector::process(const FrameInput &frame)
    {
        // 图像格式错误优先报告，其次检查时间戳和帧序。
        const FrameStamp stamp{frame.frame_id_, frame.timestamp_us_};
        const auto &previous = impl_->last_input_;
        const char *reason = nullptr;
        if (frame.image_.empty() || frame.image_.dims != 2 || frame.image_.type() != CV_8UC3)
            reason = "INPUT_FORMAT";
        else if (stamp.timestamp_us_ < 0)
            reason = "INVALID_STAMP";
        else if (previous && (stamp.frame_id_ <= previous->frame_id_ ||
                              stamp.timestamp_us_ < previous->timestamp_us_))
            reason = "INVALID_SEQUENCE";
        if (reason)
        {
            reset();
            FrameResult result;
            result.diagnostics_.push_back(reason);
            return result;
        }

        // 公开入口为解码结果补上当前帧号和时间戳。
        auto result = run_decode_pipeline(frame, impl_->config_);
        result.frame_id_ = frame.frame_id_;
        result.timestamp_us_ = frame.timestamp_us_;

        // 保存已处理帧的时间戳，供下一帧检查。
        impl_->last_input_ = stamp;

        return result;
    }

    // 清空帧序基准。
    void Detector::reset() noexcept
    {
        impl_->last_input_.reset();
    }
}
