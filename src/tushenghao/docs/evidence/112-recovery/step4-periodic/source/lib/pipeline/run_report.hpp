#pragma once
#include "core/timing.hpp"
#include <map>

namespace mark
{
    // 统计独立于采样；保存紧凑时长与枚举计数，不缓存整段视频的几何证据。
    struct RunMetadata
    {
        std::string run_id, mode, source_path, config_path, model_path, commit_label,
            code_fingerprint, input_fingerprint, run_purpose{"production"};
        std::map<std::string, std::string> environment;
    };

    struct RunSummary
    {
        uint64_t submitted{}, selected{}, exported{};
        std::array<std::vector<std::chrono::nanoseconds>, 9> durations;
        std::array<std::array<uint64_t, 5>, 9> timing_states{};
        std::map<std::string, uint64_t> statuses, reasons;
        std::optional<TimingSnapshot> first_frame;
        bool failed{false}, incomplete{false};
        std::string result_fingerprint;
        std::chrono::nanoseconds run_export{}, wall{};
        uint64_t fingerprint_frames{};
        std::chrono::nanoseconds bookkeeping{};
        std::vector<std::chrono::nanoseconds> diagnostics_construct, framework_overhead;
        void add(const FrameRecord &);
    };

    void writeRunSummary(const RunSummary &, const std::string &path);
    void writeRunManifest(const RunMetadata &, const std::string &path);

    // 紧凑指纹在process外更新，IEEE float位模式与固定little-endian，避免JSON/几何副本开销。
    class ResultFingerprint
    {
      public:
        void add(const FrameResult &);
        std::string hex() const;

        uint64_t frames() const
        {
            return frames_;
        }

      private:
        uint64_t value_{14695981039346656037ull}, frames_{};
        void bytes(uint64_t, size_t);
        void floating(float);
        void string(const std::string &);
        void detection(const Detection &);
    };
}
