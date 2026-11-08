// 检测入口校验图像与帧序，维护当前输入上下文，并返回带帧号的四角结果。
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
        bool validate_frame_stamp(const FrameStamp &c, const std::optional<FrameStamp> &p,
                                std::string &reason_)
        {
            if (c.timestamp_us_ < 0)
                reason_ = "INVALID_STAMP";
            else if (p && (c.frame_id_ <= p->frame_id_ || c.timestamp_us_ < p->timestamp_us_))
                reason_ = "INVALID_SEQUENCE";
            else
            {
                reason_.clear();
                return true;
            }
            return false;
        }
    }

    struct Detector::Impl
    {
        DetectorConfig config_;
        std::optional<FrameStamp> last_input_;
        std::optional<cv::Size> original_size_;
    };
    Detector::Detector(DetectorConfig config_) : impl_(std::make_unique<Impl>())
    {
        impl_->config_ = std::move(config_);
    }

    Detector::~Detector() = default;
    FrameResult Detector::process(const FrameInput &frame)
    {
        FrameResult result{};
        result.frame_id_ = frame.frame_id_;
        result.timestamp_us_ = frame.timestamp_us_;
        result.status_ = Status::INVALID_INPUT;
        const FrameStamp stamp{frame.frame_id_, frame.timestamp_us_};
        std::string reason_;
        if (frame.image_.empty() || frame.image_.dims != 2 || frame.image_.type() != CV_8UC3 ||
            !validate_frame_stamp(stamp, impl_->last_input_, reason_))
        {
            reset();
            result.diagnostics_.push_back(reason_.empty() ? "INPUT_FORMAT" : reason_);
            return result;
        }
        const bool size_changed = impl_->original_size_ && *impl_->original_size_ != frame.image_.size();
        if (size_changed)
            reset();
        result = run_decode_pipeline(frame, impl_->config_);
        result.frame_id_ = frame.frame_id_;
        result.timestamp_us_ = frame.timestamp_us_;
        if (size_changed)
            result.diagnostics_.push_back("INPUT_CHANGED");
        if (result.status_ == Status::INVALID_INPUT)
            reset();
        impl_->last_input_ = stamp;
        impl_->original_size_ = frame.image_.size();
        return result;
    }

    void Detector::reset() noexcept
    {
        impl_->last_input_.reset();
        impl_->original_size_.reset();
    }

} // namespace mark::detector
