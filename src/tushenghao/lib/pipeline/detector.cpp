#include "pipeline/decode_stage.hpp"
#include "mark/detector.hpp"
#include "application/app_config.hpp"
#include "detector/config.hpp"
#include "vision/types/marker_geometry.hpp"
#include "model/marker_geometry_loader.hpp"
#include "pipeline/stabilize_stage.hpp"
#include "pipeline/frame_sequence.hpp"
#include "pipeline/detector_diagnostics_access.hpp"
#include <exception>
#include <stdexcept>
#include <optional>
#include <utility>

namespace mark
{

    struct Detector::Impl
    {
        DetectorConfig config;

        // Block 2 MARK 几何模型
        MarkerGeometry marker_geometry; // 注意名字是 marker_geometry，后面构造用

        // 仅保存输入序列校验所需的元数据；不缓存图像、角点或方向。
        std::optional<FrameStamp> last_input;
        std::optional<cv::Size> original_size;
        std::unique_ptr<TemporalStabilizer> temporal;
        std::unique_ptr<DisplayHistory> display;
        FrameDiagnosticsContext diagnostics;
        DiagnosticsRequest next_request;
        std::optional<FrameRecord> completed;
        ResetSnapshot pending_resets;
        bool prepared{false}, processing{false}, require_take{false};
    };

    Detector::Detector(DetectorConfig config) : impl_(std::make_unique<Impl>())
    {
        // D01：直接构造同样核验检测参数，不借用无关的应用配置默认值。
        validateDetectorConfig(config);

        impl_->config = std::move(config); // 检查是否合法后，保存到 Detector 内部

        // 加载 Block 2 MARK 几何模型(构造函数加载 load geometry)
        impl_->marker_geometry = loadMarkerGeometry(impl_->config.marker_geometry_path_);
    }

    Detector::~Detector() = default;

    // D16旧非法分支留下缓存；公共入口统一全清，缺G-B不能用关闭模式绕过。
    FrameResult Detector::process(const FrameInput &frame)
    {
        // 入口第一项取单调时钟；覆盖诊断构造/结果装配，App再以完整公共调用替换近似边界。
        const auto started = (impl_->prepared && impl_->next_request.timing_enabled)
                                 ? std::chrono::steady_clock::now()
                                 : std::chrono::steady_clock::time_point{};
        if (impl_->processing || (impl_->require_take && impl_->completed))
            throw std::logic_error("DIAGNOSTICS_UNCONSUMED_OR_REENTRANT");
        const bool requested = impl_->prepared;
        impl_->diagnostics.begin(frame, requested ? impl_->next_request : DiagnosticsRequest{});
        impl_->require_take = requested;
        impl_->prepared = false;
        impl_->processing = true;
        impl_->diagnostics.resets(impl_->pending_resets, std::nullopt);
        impl_->pending_resets = {};

        // 异常/提前return也保留本次记录；析构只移动已构造载荷，不在total之后格式化字符串。
        struct Completion
        {
            Impl &impl;
            std::chrono::steady_clock::time_point start;
            int exceptions;

            ~Completion() noexcept
            {
                auto &c = impl.diagnostics;
                c.record.run_failed = std::uncaught_exceptions() > exceptions;
                if (c.request.timing_enabled)
                    c.timing.end(Stage::ProcessTotal,
                                 std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now() - start));
                c.finish();
                impl.completed = std::move(c.record);
                c.details.reset();
                impl.processing = false;
            }
        } completion{*impl_, started, std::uncaught_exceptions()};

        impl_->diagnostics.timing.begin(Stage::ProcessTotal);
        auto &context = impl_->diagnostics;
        // 返回前收集reset快照，不让清历史动作抹掉本帧原因；已完成帧载荷不进入历史。
        auto observe = [&](const FrameResult &r)
        {
            context.resets(impl_->pending_resets, frame.frame_id);
            impl_->pending_resets = {};
            context.output(r);
        };
        FrameResult result{};
        result.frame_id = frame.frame_id;
        result.timestamp_us = frame.timestamp_us;
        result.status = Status::INVALID_INPUT;
        FrameStamp stamp{frame.frame_id, frame.timestamp_us, frame.time_source};
        std::string reason;
        if (frame.image.empty() || frame.image.dims != 2 || frame.image.type() != CV_8UC3 ||
            !validateFrameStamp(stamp, impl_->last_input, reason))
        {
            reset(ResetReason::InvalidSequence);
            result.diagnostics.push_back(reason.empty() ? "INPUT_FORMAT" : reason);
            context.event(Stage::ProcessTotal,
                          reason.empty() ? ReasonCode::InputFormat : ReasonCode::InvalidSequence,
                          result.diagnostics.back());
            observe(result);
            return result;
        }
        const bool size_changed =
            impl_->original_size && *impl_->original_size != frame.image.size();
        if (size_changed)
            reset(ResetReason::InputChanged);
        const auto &c = impl_->config;
        if (!c.assignment_completion_ || !c.corner_.observation_budget_ ||
            !c.temporal.correspondence_uncertainty_px || !c.temporal.max_smoothing_deviation_px)
        {
            reset(ResetReason::External);
            impl_->last_input = stamp;
            impl_->original_size = frame.image.size();
            result.status = Status::NOT_READY;
            result.diagnostics.push_back("PIPELINE_BUDGET_MISSING");
            context.event(Stage::ProcessTotal, ReasonCode::BudgetMissing,
                          result.diagnostics.back());
            observe(result);
            return result;
        }
        if (!impl_->temporal)
            impl_->temporal = std::make_unique<TemporalStabilizer>(c.temporal);
        if (!impl_->display)
            impl_->display = std::make_unique<DisplayHistory>(
                DisplayHistoryConfig{c.temporal.display_hold_enabled, c.temporal.max_hold_frames});
        auto decoded = runDecodePipeline(frame, c, impl_->marker_geometry, ExecutionScope::Full, &context);
        result = finalizeDecodedFrame(decoded, stamp, frame.image.size(), *impl_->temporal,
                                      *impl_->display, &context);
        if (size_changed)
            result.diagnostics.push_back("INPUT_CHANGED");
        if (result.status == Status::INVALID_INPUT || result.status == Status::NOT_READY)
            reset(ResetReason::InvalidSequence);
        else
        {
            impl_->last_input = stamp;
            impl_->original_size = frame.image.size();
        }
        observe(result);
        return result;
    }

    // 三历史与序列/尺寸同时清，重复reset安全，不重新加载模型和配置。
    void Detector::reset(ResetReason reason) noexcept
    {
        // 原reset丢失原因；只累加无分配快照，详细事件在process内构造，不破坏noexcept。
        impl_->pending_resets.add(reason);
        impl_->last_input.reset();
        impl_->original_size.reset();
        if (impl_->temporal)
            impl_->temporal->reset(reason);
        if (impl_->display)
            impl_->display->reset(reason);
    }

    const DetectorConfig &Detector::config() const noexcept
    {
        return impl_->config;
    }

    // 请求属于目标实例，拒绝覆盖未取记录；不增加公共setter或改变历史状态。
    void DetectorDiagnosticsAccess::prepare(Detector &detector, const DiagnosticsRequest &request)
    {
        auto &impl = *detector.impl_;
        if (impl.processing || impl.prepared || (impl.require_take && impl.completed))
            throw std::logic_error("DIAGNOSTICS_PREPARE_SEQUENCE");
        impl.next_request = request;
        impl.prepared = true;
    }

    // 移动已完成观测载荷，二次读取明确失败；不重新运行算法或清除三历史。
    FrameRecord DetectorDiagnosticsAccess::take(Detector &detector)
    {
        auto &impl = *detector.impl_;
        if (impl.processing || !impl.completed)
            throw std::logic_error("DIAGNOSTICS_TAKE_SEQUENCE");
        auto record = std::move(*impl.completed);
        impl.completed.reset();
        return record;
    }

    RecordedExecution execute_and_record(Detector &detector, const FrameInput &frame,
                                         const DiagnosticsRequest &request)
    {
        using Clock = std::chrono::steady_clock;
        RecordedExecution execution;
        const auto prepare_start = Clock::now();
        DetectorDiagnosticsAccess::prepare(detector, request);
        execution.bookkeeping = Clock::now() - prepare_start;
        const auto start = Clock::now();
        try { execution.result = detector.process(frame); }
        catch (...) { execution.failure = std::current_exception(); }
        const auto elapsed = Clock::now() - start;
        const auto take_start = Clock::now();
        execution.record = DetectorDiagnosticsAccess::take(detector);
        if (execution.failure) execution.record.run_failed = true;
        else
        {
            execution.record.timings.at(size_t(Stage::ProcessTotal)) = {
                Stage::ProcessTotal, request.timing_enabled ? TimingStatus::MEASURED : TimingStatus::DISABLED,
                request.timing_enabled ? std::optional<std::chrono::nanoseconds>(elapsed) : std::nullopt};
            execution.record.process_total_boundary = "public_call";
        }
        execution.bookkeeping += Clock::now() - take_start;
        return execution;
    }

} // namespace mark
