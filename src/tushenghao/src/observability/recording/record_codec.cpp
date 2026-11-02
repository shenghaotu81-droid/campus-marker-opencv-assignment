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
                out << "{\"scheme\":" << quoteJson(d.attributes.marker_code->scheme)
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
                out << quoteJson(s[i]);
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
                << ",\"reset_or_fallback_reason\":" << quoteJson(d.reset_or_fallback_reason)
                << ",\"association\":{\"current_index\":";
            optional_number(out, d.association.current_index);
            out << ",\"matched_history\":" << (d.association.matched_history ? "true" : "false")
                << ",\"ambiguous\":" << (d.association.ambiguous ? "true" : "false")
                << ",\"reason\":" << quoteJson(d.association.reason)
                << "},\"correspondence\":{\"valid\":" << (d.correspondence.valid ? "true" : "false")
                << ",\"reason\":" << quoteJson(d.correspondence.reason) << ",\"mapping\":";
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
                out << "{\"value\":" << quoteJson(r.display_state->value)
                    << ",\"source_frame_id\":" << r.display_state->source_frame_id
                    << ",\"age\":" << r.display_state->age
                    << ",\"held\":" << (r.display_state->is_held ? "true" : "false") << '}';
            else
                out << "null";
            out << ",\"diagnostics\":";
            strings(out, r.diagnostics);
            out << '}';
        }

        // D14→A07：完整编码本次角证据；V03 依版本读取后独立复算，不能默认成 Legacy。
        void serialize_corner_evidence(JsonStream &out, const CornerEvidence &e)
        {
            out << "{\"evidence_schema_version\":" << corner_evidence_schema_version
                << ",\"physical\":" << int(e.physical_corner_) << ",\"frame_id\":" << e.frame_id_
                << ",\"component_id\":" << e.component_id_
                << ",\"model_vertex_id\":" << e.model_vertex_id_ << ",\"model_edge_ids\":["
                << e.model_edge_ids_[0] << ',' << e.model_edge_ids_[1]
                << "],\"evidence_id\":" << quoteJson(e.stable_id_)
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
                << "],\"line_model_kinds\":[" << int(e.line_model_kinds_[0]) << ','
                << int(e.line_model_kinds_[1]) << "],\"periodic_axes\":[" << e.periodic_axes_[0]
                << ',' << e.periodic_axes_[1] << "],\"periodic_coefficients\":["
                << e.periodic_coefficients_[0] << ',' << e.periodic_coefficients_[1]
                << "],\"model_mean_residual_px\":[" << e.model_mean_residual_px_[0] << ','
                << e.model_mean_residual_px_[1] << "],\"model_max_residual_px\":["
                << e.model_max_residual_px_[0] << ',' << e.model_max_residual_px_[1]
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
                out << ",\"simplification_error\":null"
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

            out << ',' << quoteJson(name) << ":{\"segmented_assignments_ready\":"
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
                    out << "{\"model_part_id\":" << quoteJson(h.assignments_[j].model_part_id_)
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
            out << (d.experimental ? *d.experimental : "null");
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
            out << "{\"record_schema_version\":1,\"run_id\":" << quoteJson(r.run_id)
                << ",\"frame_id\":" << r.frame_id
                << ",\"source_timestamp_us\":" << r.source_timestamp_us
                << ",\"timestamp_source\":" << int(r.timestamp_source)
                << ",\"timestamp_recipe\":" << quoteJson(r.timestamp_recipe)
                << ",\"execution_scope\":" << quoteJson(scopeName(r.execution_scope))
                << ",\"original_size\":[" << r.original_size.width << ',' << r.original_size.height
                << "],\"work_size\":[" << r.work_size.width << ',' << r.work_size.height
                << "],\"result_status\":";
            if (r.result_status)
                out << int(*r.result_status);
            else
                out << "null";
            out << ",\"geometry_scope_result\":"
                << (r.geometry_scope_result ? quoteJson(*r.geometry_scope_result) : "null")
                << ",\"public_status\":"
                << quoteJson(r.execution_scope == ExecutionScope::Full ? "EVALUATED" : "NOT_EVALUATED")
                << ",\"process_total_boundary\":" << quoteJson(r.process_total_boundary)
                << ",\"timings\":[";
            for (size_t i = 0; i < 9; ++i)
            {
                if (i)
                    out << ',';
                const auto &t = r.timings[i];
                out << "{\"stage\":" << quoteJson(stageName(t.stage))
                    << ",\"status\":" << quoteJson(timingStatusName(t.status)) << ",\"elapsed_us\":";
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
                << (r.last_reset_reason ? quoteJson(reasonName(*r.last_reset_reason)) : "null")
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
            out << ",\"realtime\":{\"applicability\":" << quoteJson(r.realtime_applicability)
                << ",\"slot_overwrite_count\":";
            optional_number(out, r.slot_overwrite_count);
            out << ",\"consumed_frame_count\":";
            optional_number(out, r.consumed_frame_count);
            out << ",\"enqueue_timestamp_ns\":";
            optional_number(out, r.enqueue_timestamp_ns);
            out << ",\"enqueue_to_result_us\":";
            optional_number(out, r.enqueue_to_result_us);
            out << ",\"clock_domain\":" << (r.clock_domain ? quoteJson(*r.clock_domain) : "null")
                << "},\"run_failed\":" << (r.run_failed ? "true" : "false") << ",\"events\":[";
            for (size_t i = 0; i < r.events.size(); ++i)
            {
                if (i)
                    out << ',';
                const auto &e = r.events[i];
                out << "{\"stage\":" << quoteJson(stageName(e.stage))
                    << ",\"reason\":" << quoteJson(reasonName(e.reason))
                    << ",\"detail\":" << quoteJson(e.detail) << ",\"source_frame_id\":";
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

}
