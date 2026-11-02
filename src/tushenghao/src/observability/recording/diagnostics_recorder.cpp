#include "recording/diagnostics_recorder.hpp"
#include "context/diagnostics_context.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace mark
{
    // recorder实例独立拥有生命周期，避免不同运行复用输出或串日志。
    DiagnosticsRecorder::DiagnosticsRecorder(DiagnosticsConfig c) : config_(std::move(c))
    {
    }

    // 未finish的运行必须留下失败标记，不能看起来像完整报告。
    DiagnosticsRecorder::~DiagnosticsRecorder()
    {
        if (state_ == State::Running)
            try
            {
                fail("run abandoned");
            }
            catch (...)
            {
            }
    }

    // 保留已经处理数量及失败原因，不以成功summary覆盖失败现场。
    void DiagnosticsRecorder::fail(const std::string &reason)
    {
        state_ = State::Failed;
        summary_.failed = true;
        try
        {
            std::ofstream marker(directory_ / "FAILED.json");
            marker << "{\"reason\":" << quoteJson(reason)
                   << ",\"submitted\":" << quoteJson(std::to_string(summary_.submitted)) << "}\n";
        }
        catch (...)
        {
        }
    }

    // 新建唯一run目录，已有目录拒绝，保护旧金样和用户数据。
    void DiagnosticsRecorder::beginRun(const RunMetadata &m, const std::filesystem::path &directory)
    {
        if (state_ != State::New)
            throw std::logic_error("RECORDER_BEGIN_SEQUENCE");
        if (std::filesystem::exists(directory))
            throw std::runtime_error("RUN_DIRECTORY_EXISTS");
        metadata_ = m;
        directory_ = directory;
        try
        {
            if (!std::filesystem::create_directories(directory_))
                throw std::runtime_error("cannot create run directory");
            state_ = State::Running;
            writeRunManifest(m, (directory_ / "manifest.yaml").string());
            if (config_.level != "summary")
            {
                frames_.open(directory_ / "frames.jsonl");
                side_.open(directory_ / "export_timings.jsonl");
                if (!frames_ || !side_)
                    throw std::runtime_error("cannot open records");
            }
        }
        catch (const std::exception &e)
        {
            fail(e.what());
            throw;
        }
    }

    // 所有帧进入汇总，详细选择只影响写盘；身份或I/O错误立即使运行失败。
    void DiagnosticsRecorder::submit(const FrameRecord &r)
    {
        if (state_ != State::Running)
            throw std::logic_error("RECORDER_SUBMIT_SEQUENCE");
        try
        {
            if (r.run_id != metadata_.run_id || (previous_id_ && r.frame_id <= *previous_id_))
                throw std::logic_error("RECORDER_ID_SEQUENCE");
            previous_id_ = r.frame_id;
            if (!first_id_)
                first_id_ = r.frame_id;
            previous_selected_ = r.selected;
            previous_export_pending_ =
                r.timings[size_t(Stage::Export)].status == TimingStatus::NOT_EXECUTED;

            if (r.selected && config_.level != "summary")
            {
                frames_ << serializeFrameRecord(r) << '\n';
                frames_.flush();
                if (!frames_)
                    throw std::runtime_error("record write failure");
                ++summary_.exported;
            }
            summary_.add(r);
            if (r.run_failed)
                throw std::runtime_error("algorithm exception recorded");
        }
        catch (const std::exception &e)
        {
            fail(e.what());
            throw;
        }
    }

    // 原兼容调用强制造测量；保留签名并委托唯一完成入口，避免双记账。
    void DiagnosticsRecorder::addExportTiming(uint64_t id, std::chrono::nanoseconds elapsed)
    {
        complete_export(id, {Stage::Export, TimingStatus::MEASURED, elapsed}, previous_selected_);
    }

    // 原来关闭计时仍写 MEASURED、首帧快照未更新；完成值独立于本帧 JSON，保持无自引用。
    void DiagnosticsRecorder::complete_export(uint64_t id, const StageTiming &completed,
                                              bool selected_record)
    {
        const bool measured = completed.status == TimingStatus::MEASURED;
        const bool disabled = completed.status == TimingStatus::DISABLED;
        if (state_ != State::Running || !previous_id_ || id != *previous_id_ ||
            !previous_export_pending_ || selected_record != previous_selected_ ||
            completed.stage != Stage::Export || (!measured && !disabled) ||
            measured != completed.elapsed.has_value() ||
            (completed.elapsed && completed.elapsed->count() < 0))
            throw std::logic_error("EXPORT_TIMING_SEQUENCE");
        previous_export_pending_ = false;
        const auto i = size_t(Stage::Export);
        --summary_.timing_states[i][size_t(TimingStatus::NOT_EXECUTED)];
        ++summary_.timing_states[i][size_t(completed.status)];
        if (measured)
            summary_.durations[i].push_back(*completed.elapsed);
        if (first_id_ == id && summary_.first_frame)
            (*summary_.first_frame)[i] = completed;
        auto side_start = std::chrono::steady_clock::now();
        try
        {
            // 视频可能导出未采样帧；summary 级也须留下实际发生的 Export 侧表。
            if (!side_.is_open())
                side_.open(directory_ / "export_timings.jsonl");
            side_ << "{\"run_id\":" << quoteJson(metadata_.run_id)
                  << ",\"frame_id\":" << quoteJson(std::to_string(id))
                  << ",\"status\":" << quoteJson(timingStatusName(completed.status))
                  << ",\"selected_record\":" << (selected_record ? "true" : "false")
                  << ",\"elapsed_us\":";
            if (measured)
                side_ << std::setprecision(17) << double(completed.elapsed->count()) / 1000;
            else
                side_ << "null";
            side_ << "}\n";
            side_.flush();
            if (!side_)
                throw std::runtime_error("export timing write failure");
            summary_.run_export += std::chrono::steady_clock::now() - side_start;
        }
        catch (const std::exception &e)
        {
            fail(e.what());
            throw;
        }
    }

    // 零帧和二次结束拒绝；最终文件成本单列，不能塞进最后一帧。
    RunSummary DiagnosticsRecorder::finishRun(const RunOutcome &outcome, const RunOverhead &overhead)
    {
        if (state_ != State::Running)
            throw std::logic_error("RECORDER_FINISH_SEQUENCE");
        try
        {
            if (!summary_.submitted)
                throw std::runtime_error("zero-frame run");
            if (frames_.is_open())
                frames_.flush();
            if (side_.is_open())
                side_.flush();
            if (frames_.is_open() || side_.is_open())
            {
                if ((frames_.is_open() && !frames_) || (side_.is_open() && !side_))
                    throw std::runtime_error("record flush failure");
            }
            summary_.incomplete = outcome.incomplete;
            summary_.wall = overhead.wall;
            summary_.result_fingerprint = overhead.result_fingerprint;
            summary_.fingerprint_frames = overhead.fingerprint_frames;
            auto start = std::chrono::steady_clock::now();
            writeRunSummary(summary_, (directory_ / "summary.yaml").string());
            summary_.run_export += std::chrono::steady_clock::now() - start;
            state_ = State::Finished;
            return summary_;
        }
        catch (const std::exception &e)
        {
            fail(e.what());
            throw;
        }
    }
} // namespace mark
