#include "pipeline/diagnostics_recorder.hpp"
#include "pipeline/diagnostics_context.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace mark
{
    namespace
    {
        // 旧serializer直接输出NaN/大整数，破坏JSON或精度；流适配统一有限检查和64位十进制字符串。
        struct JsonStream
        {
            std::ostringstream stream;
            size_t nonfinite{0};

            JsonStream()
            {
                stream.imbue(std::locale::classic());
                stream << std::setprecision(17);
            }

            JsonStream &operator<<(const char *s)
            {
                stream << s;
                return *this;
            }

            JsonStream &operator<<(const std::string &s)
            {
                stream << s;
                return *this;
            }

            JsonStream &operator<<(char c)
            {
                stream << c;
                return *this;
            }

            template <class T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
            JsonStream &operator<<(T v)
            {
                if constexpr (sizeof(T) >= 8)
                    stream << '"' << v << '"';
                else
                    stream << v;
                return *this;
            }

            template <class T, std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
            JsonStream &operator<<(T v)
            {
                if (std::isfinite(v))
                    stream << v;
                else
                {
                    stream << "null";
                    ++nonfinite;
                }
                return *this;
            }
        };

        inline std::string quote(const std::string &s)
        {
            std::ostringstream out;
            out << '"';
            for (unsigned char c : s)
            {
                if (c == '"' || c == '\\')
                    out << '\\' << c;
                else if (c < 32)
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c);
                else
                    out << c;
            }
            out << '"';
            return out.str();
        }

        template <class T> void optional_number(JsonStream &out, const std::optional<T> &n)
        {
            if (n)
                out << *n;
            else
                out << "null";
        }

        template <class T, size_t N> void array(JsonStream &out, const std::array<T, N> &a)
        {
            out << '[';
            for (size_t i = 0; i < N; ++i)
            {
                if (i)
                    out << ',';
                out << a[i];
            }
            out << ']';
        }

        template <class T, size_t N>
        void optional_array(JsonStream &out, const std::optional<std::array<T, N>> &a)
        {
            if (a)
                array(out, *a);
            else
                out << "null";
        }

        inline void point(JsonStream &out, cv::Point2d p)
        {
            out << '[' << p.x << ',' << p.y << ']';
        }

        inline void detection(JsonStream &out, const mark::Detection &d)
        {
            out << "{\"category\":" << int(d.category) << ",\"quality_flags\":" << d.quality_flags
                << ",\"corners\":[";
            for (size_t i = 0; i < 4; ++i)
            {
                if (i)
                    out << ',';
                point(out, d.corners[i]);
            }
            out << "],\"bbox\":[" << d.bbox.x << ',' << d.bbox.y << ',' << d.bbox.width << ','
                << d.bbox.height << "],\"orientation\":";
            optional_array(out, d.attributes.orientation);
            out << ",\"confidence\":";
            optional_number(out, d.confidence);
            out << ",\"marker_code\":";
            if (d.attributes.marker_code)
                out << "{\"scheme\":" << quote(d.attributes.marker_code->scheme)
                    << ",\"value\":" << d.attributes.marker_code->value << '}';
            else
                out << "null";
            out << '}';
        }

        inline void strings(JsonStream &out, const std::vector<std::string> &s)
        {
            out << '[';
            for (size_t i = 0; i < s.size(); ++i)
            {
                if (i)
                    out << ',';
                out << quote(s[i]);
            }
            out << ']';
        }

        inline void temporal_diagnostics(JsonStream &out, const mark::TemporalDiagnostics &d)
        {
            out << "{\"dt_seconds\":";
            optional_number(out, d.dt_seconds);
            out << ",\"alpha\":";
            optional_number(out, d.alpha);
            out << ",\"used_smoothing\":" << (d.used_smoothing ? "true" : "false")
                << ",\"fell_back\":" << (d.fell_back ? "true" : "false")
                << ",\"reset_or_fallback_reason\":" << quote(d.reset_or_fallback_reason)
                << ",\"association\":{\"current_index\":";
            optional_number(out, d.association.current_index);
            out << ",\"matched_history\":" << (d.association.matched_history ? "true" : "false")
                << ",\"ambiguous\":" << (d.association.ambiguous ? "true" : "false")
                << ",\"reason\":" << quote(d.association.reason)
                << "},\"correspondence\":{\"valid\":" << (d.correspondence.valid ? "true" : "false")
                << ",\"reason\":" << quote(d.correspondence.reason) << ",\"mapping\":";
            if (d.correspondence.valid)
                array(out, d.correspondence.current_to_previous);
            else
                out << "null";
            out << ",\"energy\":";
            optional_array(out, d.correspondence.energy);
            out << ",\"lower\":";
            optional_array(out, d.correspondence.lower);
            out << ",\"upper\":";
            optional_array(out, d.correspondence.upper);
            out << "},\"output_slot_mapping\":";
            optional_array(out, d.output_slot_mapping);
            out << '}';
        }

        inline void frame_result(JsonStream &out, const mark::FrameResult &r)
        {
            out << "{\"status\":" << int(r.status) << ",\"detections\":[";
            for (size_t i = 0; i < r.detections.size(); ++i)
            {
                if (i)
                    out << ',';
                detection(out, r.detections[i]);
            }
            out << "],\"tracks\":[";
            for (size_t i = 0; i < r.tracks.size(); ++i)
            {
                if (i)
                    out << ',';
                out << "{\"detection_index\":" << r.tracks[i].detection_index << ",\"result\":";
                detection(out, r.tracks[i].result);
                out << '}';
            }
            out << "],\"display\":";
            if (r.display_state)
                out << "{\"value\":" << quote(r.display_state->value)
                    << ",\"source_frame_id\":" << r.display_state->source_frame_id
                    << ",\"age\":" << r.display_state->age
                    << ",\"held\":" << (r.display_state->is_held ? "true" : "false") << '}';
            else
                out << "null";
            out << ",\"diagnostics\":";
            strings(out, r.diagnostics);
            out << '}';
        }

        // 原角证据深嵌测量循环；独立输出真实证据，保持字段次序和同一次取证来源。
        void serialize_corner_evidence(JsonStream &out, const CornerEvidence &e)
        {
            out << "{\"physical\":" << int(e.physical_corner_) << ",\"frame_id\":" << e.frame_id_
                << ",\"component_id\":" << e.component_id_
                << ",\"model_vertex_id\":" << e.model_vertex_id_ << ",\"model_edge_ids\":["
                << e.model_edge_ids_[0] << ',' << e.model_edge_ids_[1]
                << "],\"evidence_id\":" << quote(e.stable_id_)
                << ",\"original_observation\":" << (e.original_observation_ ? "true" : "false")
                << ",\"truncated\":" << (e.truncated_ ? "true" : "false")
                << ",\"observed_segment_ids\":[";
            for (size_t i = 0; i < e.observed_segment_ids_.size(); ++i)
            {
                if (i)
                    out << ',';
                out << e.observed_segment_ids_[i];
            }
            out << "],\"observed_segment_end_ids\":[" << e.observed_segment_end_ids_[0] << ','
                << e.observed_segment_end_ids_[1] << "],\"fitted_lines\":[";
            for (int a = 0; a < 2; ++a)
            {
                if (a)
                    out << ',';
                auto fit = a == 0 ? e.line_a_ : e.line_b_;
                out << '[';
                for (int i = 0; i < 4; ++i)
                {
                    if (i)
                        out << ',';
                    out << fit[i];
                }
                out << ']';
            }
            out << "],\"finite_segments\":[";
            for (int a = 0; a < 2; ++a)
            {
                if (a)
                    out << ',';
                const auto &segment = a == 0 ? e.edge_segment_a_ : e.edge_segment_b_;
                out << '[';
                point(out, segment[0]);
                out << ',';
                point(out, segment[1]);
                out << ']';
            }
            out << "],\"intersection\":";
            point(out, e.intersection_);
            out << ",\"line_mean_residual_px\":[" << e.line_mean_residual_px_[0] << ','
                << e.line_mean_residual_px_[1] << "],\"line_max_residual_px\":["
                << e.line_max_residual_px_[0] << ',' << e.line_max_residual_px_[1]
                << "],\"support_extension_px\":[" << e.support_extension_px_[0] << ','
                << e.support_extension_px_[1] << "],\"corner_error_px\":" << e.corner_error_px_
                << ",\"measurement_error_px\":" << e.error_ << ",\"support_arcs\":[";
            for (int a = 0; a < 2; ++a)
            {
                if (a)
                    out << ',';
                out << '[';
                for (size_t p = 0; p < e.original_support_arcs_[a].size(); ++p)
                {
                    if (p)
                        out << ',';
                    point(out, e.original_support_arcs_[a][p]);
                }
                out << ']';
            }
            out << "],\"turn_arc\":[";
            for (size_t p = 0; p < e.original_turn_arc_.size(); ++p)
            {
                if (p)
                    out << ',';
                point(out, e.original_turn_arc_[p]);
            }
            out << "]}";
        }

        // 完整记录同次decode的原始观测证据，稳定输出不覆盖交点或支持弧。
        inline void decode_payload(JsonStream &out, const mark::DecodeStageResult &r)
        {
            out << "{\"status\":" << int(r.status)
                << ",\"search_truncated\":" << (r.search_truncated ? "true" : "false")
                << ",\"detections\":[";
            for (size_t i = 0; i < r.detections.size(); ++i)
            {
                if (i)
                    out << ',';
                detection(out, r.detections[i]);
            }
            out << "],\"measurements\":[";
            for (size_t m = 0; m < r.measurements.size(); ++m)
            {
                if (m)
                    out << ',';
                out << '[';
                for (int c = 0; c < 4; ++c)
                {
                    if (c)
                        out << ',';
                    serialize_corner_evidence(out, r.measurements[m].evidence_[c]);
                }
                out << ']';
            }
            out << "],\"diagnostics\":";
            strings(out, r.diagnostics);
            out << '}';
        }

        // 原详情序列化嵌在超长函数；按载荷职责拆出，字节顺序及空值规则保持一致。
        void serialize_components(JsonStream &out, const StageDetails &d)
        {
            for (size_t i = 0; i < d.components.size(); ++i)
            {
                if (i)
                    out << ',';
                const auto &c = d.components[i];
                out << "{\"component_id\":" << c.component_id_ << ",\"bbox_work\":["
                    << c.bounding_box_.x << ',' << c.bounding_box_.y << ',' << c.bounding_box_.width
                    << ',' << c.bounding_box_.height << "],\"area\":" << c.area_
                    << ",\"touches_border\":" << (c.touches_border_ ? "true" : "false")
                    << ",\"contour\":[";
                for (size_t j = 0; j < c.contour_.size(); ++j)
                {
                    if (j)
                        out << ',';
                    point(out, c.contour_[j]);
                }
                out << "]}";
            }
        }

        // 原详情序列化嵌在超长函数；按载荷职责拆出，字节顺序及空值规则保持一致。
        void serialize_observations(JsonStream &out, const StageDetails &d)
        {
            for (size_t i = 0; i < d.observations.size(); ++i)
            {
                if (i)
                    out << ',';
                const auto &o = d.observations[i];
                out << "{\"source_component_id\":" << o.source_component_id_ << ",\"polygon\":[";
                for (size_t j = 0; j < o.simplified_polygon_.size(); ++j)
                {
                    if (j)
                        out << ',';
                    point(out, o.simplified_polygon_[j]);
                }
                out << "],\"supported_classes\":";
                strings(out, o.supported_classes_);
                out << ",\"simplification_error\":" << o.simplification_error_
                    << ",\"anchor_vertex_index\":";
                optional_number(out, o.anchor_vertex_index_);
                out << ",\"turns\":[";
                for (size_t k = 0; k < o.turns_.size(); ++k)
                {
                    if (k)
                        out << ',';
                    out << "{\"vertex_index\":" << o.turns_[k].vertex_index_
                        << ",\"type\":" << int(o.turns_[k].type_) << '}';
                }
                out << "],\"l_topology_candidates\":[";
                for (size_t k = 0; k < o.l_topology_candidates_.size(); ++k)
                {
                    if (k)
                        out << ',';
                    const auto &t = o.l_topology_candidates_[k];
                    out << "{\"polygon\":[";
                    for (size_t n = 0; n < t.polygon_.size(); ++n)
                    {
                        if (n)
                            out << ',';
                        point(out, t.polygon_[n]);
                    }
                    out << "],\"concave_vertex_indices\":[";
                    for (size_t n = 0; n < t.concave_vertex_indices_.size(); ++n)
                    {
                        if (n)
                            out << ',';
                        out << t.concave_vertex_indices_[n];
                    }
                    out << "],\"simplification_epsilon\":" << t.simplification_epsilon_ << '}';
                }
                out << "]}";
            }
        }

        // 原详情序列化嵌在超长函数；按载荷职责拆出，字节顺序及空值规则保持一致。
        void serialize_batch(JsonStream &out, const char *name, const GeometryBatch &b)
        {

            out << ',' << quote(name) << ":{\"segmented_assignments_ready\":"
                << (b.segmented_assignments_ready_ ? "true" : "false")
                << ",\"truncated\":" << (b.resource_truncated_ ? "true" : "false")
                << ",\"hypotheses\":[";
            for (size_t i = 0; i < b.hypotheses_.size(); ++i)
            {
                if (i)
                    out << ',';
                const auto &h = b.hypotheses_[i];
                out << "{\"id\":" << uint64_t(i) << ",\"assignments\":[";
                for (size_t j = 0; j < h.assignments_.size(); ++j)
                {
                    if (j)
                        out << ',';
                    out << "{\"model_part_id\":" << quote(h.assignments_[j].model_part_id_)
                        << ",\"component_id\":" << h.assignments_[j].component_id_ << '}';
                }
                out << "],\"affine\":[";
                if (h.affine_transform_.rows == 2 && h.affine_transform_.cols == 3 &&
                    h.affine_transform_.type() == CV_64F)
                    for (int row = 0; row < 2; ++row)
                        for (int col = 0; col < 3; ++col)
                        {
                            if (row || col)
                                out << ',';
                            out << h.affine_transform_.at<double>(row, col);
                        }
                out << "],\"validation_residual\":" << h.validation_residual_
                    << ",\"completeness\":" << int(h.completeness_) << ",\"evidence\":";
                strings(out, h.evidence_);
                out << '}';
            }
            out << "],\"diagnostics\":";
            strings(out, b.diagnostics_);
            out << '}';
        }

        // 原详情序列化嵌在超长函数；按载荷职责拆出，字节顺序及空值规则保持一致。
        void serialize_experiment(JsonStream &out, const StageDetails &d)
        {
            if (d.experimental)
            {
                const auto &e = *d.experimental;
                out << "{\"segment\":" << e.segment << ",\"sample\":" << e.sample
                    << ",\"dt_ms\":" << e.dt_ms << ",\"mode\":" << e.mode << ",\"rate\":" << e.rate
                    << ",\"r_experimental_px\":" << e.r_px
                    << ",\"deviation_experimental_px\":" << e.deviation_px
                    << ",\"known\":" << (e.known ? "true" : "false")
                    << ",\"noisy\":" << (e.noisy ? "true" : "false") << ",\"truth_physical\":[";
                for (size_t i = 0; i < 4; ++i)
                {
                    if (i)
                        out << ',';
                    point(out, e.truth_physical[i]);
                }
                out << "],\"raw_physical\":[";
                for (size_t i = 0; i < 4; ++i)
                {
                    if (i)
                        out << ',';
                    point(out, e.raw_physical[i]);
                }
                out << "]}";
            }
            else
                out << "null";
        }

        // 原详情序列化嵌在超长函数；按载荷职责拆出，字节顺序及空值规则保持一致。
        void serialize_details(JsonStream &out, const FrameRecord &r)
        {
            out << ",\"details\":";
            if (r.details && (r.counts.generated || r.counts.tracks || r.details->experimental))
            {
                const auto &d = *r.details;
                out << "{\"components\":[";
                serialize_components(out, d);
                out << "],\"observations\":[";
                serialize_observations(out, d);
                out << ']';
                serialize_batch(out, "generated", d.generated);
                serialize_batch(out, "validated", d.validated);
                serialize_batch(out, "completed", d.completed);
                out << ",\"decode\":";
                if (r.counts.measurements)
                    decode_payload(out, d.decoded);
                else
                    out << "null";
                out << ",\"temporal\":";
                if (r.execution_scope == ExecutionScope::Full ||
                    r.execution_scope == ExecutionScope::Temporal)
                    temporal_diagnostics(out, d.temporal);
                else
                    out << "null";
                out << ",\"experimental\":";
                serialize_experiment(out, d);
                out << '}';
            }
            else
                out << "null";
        }

        // 原帧字段与嵌套详情混排；按原顺序输出同一字段，不增加第二套格式。
        void serialize_header(JsonStream &out, const FrameRecord &r)
        {
            out << "{\"record_schema_version\":1,\"run_id\":" << quote(r.run_id)
                << ",\"frame_id\":" << r.frame_id
                << ",\"source_timestamp_us\":" << r.source_timestamp_us
                << ",\"timestamp_source\":" << int(r.timestamp_source)
                << ",\"timestamp_recipe\":" << quote(r.timestamp_recipe)
                << ",\"execution_scope\":" << quote(scopeName(r.execution_scope))
                << ",\"original_size\":[" << r.original_size.width << ',' << r.original_size.height
                << "],\"work_size\":[" << r.work_size.width << ',' << r.work_size.height
                << "],\"result_status\":";
            if (r.result_status)
                out << int(*r.result_status);
            else
                out << "null";
            out << ",\"geometry_scope_result\":"
                << (r.geometry_scope_result ? quote(*r.geometry_scope_result) : "null")
                << ",\"public_status\":"
                << quote(r.execution_scope == ExecutionScope::Full ? "EVALUATED" : "NOT_EVALUATED")
                << ",\"process_total_boundary\":" << quote(r.process_total_boundary)
                << ",\"timings\":[";
            for (size_t i = 0; i < 9; ++i)
            {
                if (i)
                    out << ',';
                const auto &t = r.timings[i];
                out << "{\"stage\":" << quote(stageName(t.stage))
                    << ",\"status\":" << quote(timingStatusName(t.status)) << ",\"elapsed_us\":";
                if (t.status == TimingStatus::MEASURED && t.elapsed)
                    out << double(t.elapsed->count()) / 1000;
                else
                    out << "null";
                out << '}';
            }
        }

        // 原帧字段与嵌套详情混排；按原顺序输出同一字段，不增加第二套格式。
        void serialize_counts(JsonStream &out, const FrameRecord &r)
        {
            out << "],\"counts\":{\"components\":";
            optional_number(out, r.counts.components);
            out << ",\"observations\":";
            optional_number(out, r.counts.observations);
            out << ",\"generated\":";
            optional_number(out, r.counts.generated);
            out << ",\"validated\":";
            optional_number(out, r.counts.validated);
            out << ",\"completed\":";
            optional_number(out, r.counts.completed);
            out << ",\"measurements\":";
            optional_number(out, r.counts.measurements);
            out << ",\"detections\":";
            optional_number(out, r.counts.detections);
            out << ",\"tracks\":";
            optional_number(out, r.counts.tracks);
            out << ",\"truncated\":" << (r.counts.truncated ? "true" : "false") << "},\"result\":";
            if (r.output)
                frame_result(out, *r.output);
            else
                out << "null";
            out << ",\"last_reset_reason\":"
                << (r.last_reset_reason ? quote(reasonName(*r.last_reset_reason)) : "null")
                << ",\"last_reset_source_frame_id\":";
            optional_number(out, r.last_reset_source_frame_id);
            out << ",\"diagnostics_construct_intervals_us\":";
            if (r.diagnostics_construct_time)
                out << double(r.diagnostics_construct_time->count()) / 1000;
            else
                out << "null";
        }

        // 原帧字段与嵌套详情混排；按原顺序输出同一字段，不增加第二套格式。
        void serialize_tail(JsonStream &out, const FrameRecord &r)
        {
            out << ",\"realtime\":{\"applicability\":" << quote(r.realtime_applicability)
                << ",\"slot_overwrite_count\":";
            optional_number(out, r.slot_overwrite_count);
            out << ",\"consumed_frame_count\":";
            optional_number(out, r.consumed_frame_count);
            out << ",\"enqueue_timestamp_ns\":";
            optional_number(out, r.enqueue_timestamp_ns);
            out << ",\"enqueue_to_result_us\":";
            optional_number(out, r.enqueue_to_result_us);
            out << ",\"clock_domain\":" << (r.clock_domain ? quote(*r.clock_domain) : "null")
                << "},\"run_failed\":" << (r.run_failed ? "true" : "false") << ",\"events\":[";
            for (size_t i = 0; i < r.events.size(); ++i)
            {
                if (i)
                    out << ',';
                const auto &e = r.events[i];
                out << "{\"stage\":" << quote(stageName(e.stage))
                    << ",\"reason\":" << quote(reasonName(e.reason))
                    << ",\"detail\":" << quote(e.detail) << ",\"source_frame_id\":";
                optional_number(out, e.source_frame_id);
                out << ",\"hypothesis_id\":";
                optional_number(out, e.hypothesis_id);
                out << ",\"component_id\":";
                optional_number(out, e.component_id);
                out << ",\"occurrences\":" << e.occurrences << '}';
            }
            if (out.nonfinite)
            {
                if (!r.events.empty())
                    out << ',';
                out << "{\"stage\":\"export\",\"reason\":\"NonFiniteField\",\"detail\":\"non-finite fields "
                       "serialized as null\",\"source_frame_id\":"
                    << r.frame_id
                    << ",\"hypothesis_id\":null,\"component_id\":null,\"occurrences\":"
                    << uint64_t(out.nonfinite) << '}';
            }
        }
    } // namespace

    std::string quoteJson(const std::string &s)
    {
        return quote(s);
    }

    // 所有stage/audit共用此schema；只有选中详情才复制/输出证据，非有限字段追加明确事件。
    std::string serializeFrameRecord(const FrameRecord &r)
    {
        JsonStream out;
        serialize_header(out, r);
        serialize_counts(out, r);
        serialize_details(out, r);
        serialize_tail(out, r);
        out << "]}";
        return out.stream.str();
    }

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
            (completed.elapsed && completed.elapsed->count() < 0) ||
            std::find(export_ids_.begin(), export_ids_.end(), id) != export_ids_.end())
            throw std::logic_error("EXPORT_TIMING_SEQUENCE");
        export_ids_.push_back(id);
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
    RunSummary DiagnosticsRecorder::finishRun()
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
