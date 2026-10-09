#pragma once
#include "config/observability_config.hpp"
#include "pipeline/run_report.hpp"
#include <filesystem>
#include <fstream>

namespace mark
{
    std::string quoteJson(const std::string &);
    std::string serializeFrameRecord(const FrameRecord &);

    // begin/submit/finish严格生命周期，已有目录与IO失败拒绝；失败只留标记，不造成功summary。
    class DiagnosticsRecorder
    {
      public:
        explicit DiagnosticsRecorder(DiagnosticsConfig);
        ~DiagnosticsRecorder();
        void beginRun(const RunMetadata &, const std::filesystem::path &);
        void submit(const FrameRecord &);
        void addExportTiming(uint64_t, std::chrono::nanoseconds);
        void complete_export(std::uint64_t frame_id, const StageTiming &completed,
                             bool selected_record);
        RunSummary finishRun();

        RunSummary &summary()
        {
            return summary_;
        }

        const std::filesystem::path &directory() const
        {
            return directory_;
        }

      private:
        DiagnosticsConfig config_;
        RunMetadata metadata_;
        std::filesystem::path directory_;
        std::ofstream frames_, side_;
        RunSummary summary_;
        enum class State
        {
            New,
            Running,
            Finished,
            Failed
        };
        State state_{State::New};
        std::optional<uint64_t> first_id_;
        bool previous_selected_{false}, previous_export_pending_{false};
        std::optional<uint64_t> previous_id_;
        std::vector<uint64_t> export_ids_;
        void fail(const std::string &);
    };
} // namespace mark
