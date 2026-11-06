#include "vision/detection/detector.hpp"
#include <optional>
#include "detector/config.hpp"
#include "pipeline/decode/decode_stage.hpp"
#include "pipeline/input/frame_sequence.hpp"
namespace mark
{
    struct Detector::Impl
    {
        DetectorConfig config;
        std::optional<FrameStamp> last_input;
        std::optional<cv::Size> original_size;
    };
    Detector::Detector(DetectorConfig config) : impl_(std::make_unique<Impl>())
    {
        validateDetectorConfig(config);
        impl_->config = std::move(config);
    }

    Detector::~Detector() = default;
    FrameResult Detector::process(const FrameInput &frame)
    {
        FrameResult result{};
        result.frame_id = frame.frame_id;
        result.timestamp_us = frame.timestamp_us;
        result.status = Status::INVALID_INPUT;
        const FrameStamp stamp{frame.frame_id, frame.timestamp_us};
        std::string reason;
        if (frame.image.empty() || frame.image.dims != 2 || frame.image.type() != CV_8UC3 ||
            !validateFrameStamp(stamp, impl_->last_input, reason))
        {
            reset();
            result.diagnostics.push_back(reason.empty() ? "INPUT_FORMAT" : reason);
            return result;
        }
        const bool size_changed = impl_->original_size && *impl_->original_size != frame.image.size();
        if (size_changed)
            reset();
        auto decoded = runDecodePipeline(frame, impl_->config);
        result.status = decoded.status;
        result.detections = std::move(decoded.detections);
        result.diagnostics = std::move(decoded.diagnostics);
        if (size_changed)
            result.diagnostics.push_back("INPUT_CHANGED");
        if (result.status == Status::INVALID_INPUT)
            reset();
        impl_->last_input = stamp;
        impl_->original_size = frame.image.size();
        return result;
    }

    void Detector::reset() noexcept
    {
        impl_->last_input.reset();
        impl_->original_size.reset();
    }

} // namespace mark
