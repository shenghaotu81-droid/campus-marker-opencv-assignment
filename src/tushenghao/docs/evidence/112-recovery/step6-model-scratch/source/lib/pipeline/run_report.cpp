#include "pipeline/run_report.hpp"
#include "pipeline/diagnostics_recorder.hpp"
#include <fstream>
#include <iomanip>
#include <locale>
#include <stdexcept>

namespace mark
{
    // 每帧都进入分母；未执行耗时不填零，框架差额只在同边界的四阶段均实测时计算。
    void RunSummary::add(const FrameRecord &r)
    {
        ++submitted;
        if (r.selected)
            ++selected;
        if (!first_frame)
            first_frame = r.timings;
        failed = failed || r.run_failed;
        if (r.result_status)
            ++statuses[std::to_string(int(*r.result_status))];
        else if (r.geometry_scope_result)
            ++statuses[*r.geometry_scope_result];
        for (size_t i = 0; i < 9; ++i)
        {
            const auto &t = r.timings[i];
            if (t.stage != Stage(i) ||
                (t.status == TimingStatus::MEASURED) != t.elapsed.has_value() ||
                (t.elapsed && t.elapsed->count() < 0))
                throw std::logic_error("INVALID_TIMING_RECORD");
            ++timing_states[i][size_t(t.status)];
            if (t.status == TimingStatus::MEASURED)
                durations[i].push_back(*t.elapsed);
        }
        if (r.diagnostics_construct_time)
            diagnostics_construct.push_back(*r.diagnostics_construct_time);

        bool complete = true;
        std::chrono::nanoseconds stages{};
        for (size_t i = 1; i <= 4; ++i)
        {
            if (r.timings[i].status != TimingStatus::MEASURED)
                complete = false;
            else
                stages += *r.timings[i].elapsed;
        }

        if (complete && r.timings[5].status == TimingStatus::MEASURED)
        {
            auto overhead = *r.timings[5].elapsed - stages;
            if (overhead.count() < 0)
                throw std::logic_error("NEGATIVE_FRAMEWORK_OVERHEAD");
            framework_overhead.push_back(overhead);
        }
        for (const auto &e : r.events)
            reasons[reasonName(e.reason)] += e.occurrences;
    }

    // YAML使用明确null和64位十进制字符串；不让FileStorage把缺失统计改成0或丢整数精度。
    void writeRunSummary(const RunSummary &s, const std::string &path)
    {
        std::ofstream out(path);
        out.imbue(std::locale::classic());
        out << std::setprecision(17);
        if (!out)
            throw std::runtime_error("cannot open summary");
        out << "record_schema_version: 1\nsubmitted: " << quoteJson(std::to_string(s.submitted))
            << "\nselected: " << quoteJson(std::to_string(s.selected))
            << "\nexported: " << quoteJson(std::to_string(s.exported))
            << "\nfailed: " << (s.failed ? "true" : "false")
            << "\nincomplete: " << (s.incomplete ? "true" : "false")
            << "\nwall_us: " << double(s.wall.count()) / 1000
            << "\nrun_export_us: " << double(s.run_export.count()) / 1000
            << "\nfingerprint_algorithm: FNV1a64\nfingerprint_version: 1\nresult_fingerprint: "
            << quoteJson(s.result_fingerprint)
            << "\nfingerprint_frames: " << quoteJson(std::to_string(s.fingerprint_frames))
            << "\nstages:\n";
        for (size_t i = 0; i < 9; ++i)
        {
            auto stats = summarizeDurations(s.durations[i]);
            out << "  " << stageName(Stage(i)) << ":\n    N: " << quoteJson(std::to_string(stats.n))
                << '\n';
            const std::pair<const char *, std::optional<double>> fields[] = {
                {"mean_us", stats.mean_ns},
                {"median_us", stats.median_ns},
                {"p95_us", stats.p95_ns},
                {"p99_us", stats.p99_ns},
                {"max_us", stats.max_ns}};
            for (const auto &f : fields)
            {
                out << "    " << f.first << ": ";
                if (f.second)
                    out << *f.second / 1000;
                else
                    out << "null";
                out << '\n';
            }
            out << "    statuses:\n";
            for (size_t j = 0; j < 5; ++j)
                out << "      " << timingStatusName(TimingStatus(j)) << ": "
                    << quoteJson(std::to_string(s.timing_states[i][j])) << '\n';
            out << "    first_frame_us: ";
            if (s.first_frame && (*s.first_frame)[i].elapsed)
                out << double((*s.first_frame)[i].elapsed->count()) / 1000;
            else
                out << "null";
            out << '\n';
        }
        out << "bookkeeping_us: " << double(s.bookkeeping.count()) / 1000 << '\n';
        for (const auto &group :
             {std::make_pair("diagnostics_construct_intervals", &s.diagnostics_construct),
              std::make_pair("framework_overhead", &s.framework_overhead)})
        {
            auto stats = summarizeDurations(*group.second);
            out << group.first << ":\n  N: " << quoteJson(std::to_string(stats.n))
                << "\n  mean_us: ";
            if (stats.mean_ns)
                out << *stats.mean_ns / 1000;
            else
                out << "null";
            out << "\n  p95_us: ";
            if (stats.p95_ns)
                out << *stats.p95_ns / 1000;
            else
                out << "null";
            out << '\n';
        }
        out << "result_status_counts:\n";
        for (const auto &p : s.statuses)
            out << "  " << quoteJson(p.first) << ": " << quoteJson(std::to_string(p.second))
                << '\n';
        out << "reason_counts:\n";
        for (const auto &p : s.reasons)
            out << "  " << quoteJson(p.first) << ": " << quoteJson(std::to_string(p.second))
                << '\n';
        out.flush();
        if (!out)
            throw std::runtime_error("summary write failure");
    }

    // 记录真实来源、工作树指纹与环境，不把外部提交标签冒称当前实现提交。
    void writeRunManifest(const RunMetadata &m, const std::string &path)
    {
        std::ofstream out(path);
        if (!out)
            throw std::runtime_error("cannot open manifest");
        out << "record_schema_version: 1\n";
        const std::pair<const char *, std::string> fields[] = {
            {"run_id", m.run_id},
            {"mode", m.mode},
            {"source_path", m.source_path},
            {"config_path", m.config_path},
            {"model_path", m.model_path},
            {"commit_label", m.commit_label},
            {"code_sha256", m.code_fingerprint},
            {"input_fingerprint", m.input_fingerprint},
            {"run_purpose", m.run_purpose}};
        for (const auto &p : fields)
            out << p.first << ": " << quoteJson(p.second) << '\n';
        out << "environment:\n";
        for (const auto &p : m.environment)
            out << "  " << quoteJson(p.first) << ": " << quoteJson(p.second) << '\n';
        out.flush();
        if (!out)
            throw std::runtime_error("manifest write failure");
    }
}

#include <cstring>
#include <sstream>

namespace mark
{
    // 固定小端字节顺序，使结果链可跨序列化格式复算。
    void ResultFingerprint::bytes(uint64_t v, size_t n)
    {
        for (size_t i = 0; i < n; ++i)
        {
            value_ ^= (v >> (i * 8)) & 255;
            value_ *= 1099511628211ull;
        }
    }

    // 按float实际位模式累积，避免十进制舍入掩盖有效点变化。
    void ResultFingerprint::floating(float v)
    {
        static_assert(sizeof(float) == 4, "IEEE float required");
        uint32_t bits;
        std::memcpy(&bits, &v, 4);
        bytes(bits, 4);
    }

    // 长度前缀与UTF8字节共同入链，避免不同字段拼接碰撞。
    void ResultFingerprint::string(const std::string &s)
    {
        bytes(s.size(), 8);
        for (unsigned char c : s)
            bytes(c, 1);
    }

    // 只纳入公开检测属性，不把诊断、计时或run身份混进结果一致性。
    void ResultFingerprint::detection(const Detection &d)
    {
        bytes(uint32_t(d.category), 4);
        bytes(d.quality_flags, 4);
        for (auto p : d.corners)
        {
            floating(p.x);
            floating(p.y);
        }
        floating(d.bbox.x);
        floating(d.bbox.y);
        floating(d.bbox.width);
        floating(d.bbox.height);
        bytes(d.attributes.orientation.has_value(), 1);
        if (d.attributes.orientation)
            for (int p : *d.attributes.orientation)
                bytes(uint32_t(p), 4);
        bytes(d.attributes.marker_code.has_value(), 1);
        if (d.attributes.marker_code)
        {
            string(d.attributes.marker_code->scheme);
            bytes(uint32_t(d.attributes.marker_code->value), 4);
        }
        bytes(d.confidence.has_value(), 1);
        if (d.confidence)
            floating(*d.confidence);
    }

    // 逐帧追加raw、稳定、display；空帧和首帧同样保留在链中。
    void ResultFingerprint::add(const FrameResult &r)
    {
        bytes(r.frame_id, 8);
        bytes(uint64_t(r.timestamp_us), 8);
        bytes(uint32_t(r.status), 4);
        bytes(r.detections.size(), 8);
        for (const auto &d : r.detections)
            detection(d);
        bytes(r.tracks.size(), 8);
        for (const auto &t : r.tracks)
        {
            bytes(t.detection_index, 8);
            detection(t.result);
        }
        bytes(r.display_state.has_value(), 1);
        if (r.display_state)
        {
            bytes(r.display_state->source_frame_id, 8);
            bytes(r.display_state->age, 8);
            bytes(r.display_state->is_held, 1);
            string(r.display_state->value);
        }
        ++frames_;
    }

    // 导出固定宽度十六进制，便于baseline与详细验证记录相连。
    std::string ResultFingerprint::hex() const
    {
        std::ostringstream out;
        out << std::hex << std::setw(16) << std::setfill('0') << value_;
        return out.str();
    }
}
