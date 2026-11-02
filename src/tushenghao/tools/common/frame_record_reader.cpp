#include "common/frame_record_reader.hpp"
#include "vision/types/detection/corner_types.hpp"
#include <charconv>
#include <cmath>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace mark::validation
{
    const JsonValue &JsonValue::at(const std::string &k) const
    {
        if (kind != Kind::Object)
            throw std::runtime_error("expected JSON object");
        return object.at(k);
    }

    double JsonValue::number() const
    {
        if (kind != Kind::Number)
            throw std::runtime_error("expected JSON number");
        std::istringstream s(text);
        s.imbue(std::locale::classic());
        double n;
        s >> n;
        if (!s || !s.eof() || !std::isfinite(n))
            throw std::runtime_error("invalid finite number");
        return n;
    }

    uint64_t JsonValue::u64() const
    {
        if (kind != Kind::String && kind != Kind::Number)
            throw std::runtime_error("expected uint64 token");
        uint64_t n;
        auto r = std::from_chars(text.data(), text.data() + text.size(), n);
        if (r.ec != std::errc{} || r.ptr != text.data() + text.size())
            throw std::runtime_error("invalid uint64");
        return n;
    }

    int64_t JsonValue::i64() const
    {
        if (kind != Kind::String && kind != Kind::Number)
            throw std::runtime_error("expected int64 token");
        int64_t n;
        auto r = std::from_chars(text.data(), text.data() + text.size(), n);
        if (r.ec != std::errc{} || r.ptr != text.data() + text.size())
            throw std::runtime_error("invalid int64");
        return n;
    }

    bool JsonValue::operator==(const JsonValue &v) const
    {
        return kind == v.kind && text == v.text && boolean == v.boolean && array == v.array &&
               object == v.object;
    }

    namespace
    {
        // 不使用FileStorage读取null JSON；明确拒重复键、非法escape、截断与尾随内容。
        class Parser
        {
            const std::string &s;
            size_t p{0};

            [[noreturn]] void error() const
            {
                throw std::runtime_error("invalid JSON at byte " + std::to_string(p));
            }

            void whitespace()
            {
                while (p < s.size() &&
                       (s[p] == ' ' || s[p] == '\n' || s[p] == '\r' || s[p] == '\t'))
                    ++p;
            }

            char get()
            {
                if (p == s.size())
                    error();
                return s[p++];
            }

            bool consume(char c)
            {
                whitespace();
                if (p < s.size() && s[p] == c)
                {
                    ++p;
                    return true;
                }
                return false;
            }

            unsigned hex()
            {
                unsigned n = 0;
                for (int i = 0; i < 4; ++i)
                {
                    char c = get();
                    n *= 16;
                    if (c >= '0' && c <= '9')
                        n += c - '0';
                    else if (c >= 'a' && c <= 'f')
                        n += c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F')
                        n += c - 'A' + 10;
                    else
                        error();
                }
                return n;
            }

            void utf8(std::string &out, unsigned c)
            {
                if (c <= 0x7f)
                    out += char(c);
                else if (c <= 0x7ff)
                {
                    out += char(0xc0 | (c >> 6));
                    out += char(0x80 | (c & 63));
                }
                else if (c <= 0xffff)
                {
                    out += char(0xe0 | (c >> 12));
                    out += char(0x80 | ((c >> 6) & 63));
                    out += char(0x80 | (c & 63));
                }
                else
                {
                    out += char(0xf0 | (c >> 18));
                    out += char(0x80 | ((c >> 12) & 63));
                    out += char(0x80 | ((c >> 6) & 63));
                    out += char(0x80 | (c & 63));
                }
            }

            std::string string()
            {
                if (get() != '"')
                    error();
                std::string out;
                for (;;)
                {
                    char c = get();
                    if (c == '"')
                        return out;
                    if (static_cast<unsigned char>(c) < 32)
                        error();
                    if (c != '\\')
                    {
                        out += c;
                        continue;
                    }
                    switch (get())
                    {
                    case '"':
                        out += '"';
                        break;
                    case '\\':
                        out += '\\';
                        break;
                    case '/':
                        out += '/';
                        break;
                    case 'b':
                        out += '\b';
                        break;
                    case 'f':
                        out += '\f';
                        break;
                    case 'n':
                        out += '\n';
                        break;
                    case 'r':
                        out += '\r';
                        break;
                    case 't':
                        out += '\t';
                        break;
                    case 'u':
                    {
                        unsigned h = hex();
                        if (h >= 0xd800 && h <= 0xdbff)
                        {
                            if (get() != '\\' || get() != 'u')
                                error();
                            unsigned l = hex();
                            if (l < 0xdc00 || l > 0xdfff)
                                error();
                            h = 0x10000 + ((h - 0xd800) << 10) + (l - 0xdc00);
                        }
                        else if (h >= 0xdc00 && h <= 0xdfff)
                            error();
                        utf8(out, h);
                        break;
                    }
                    default:
                        error();
                    }
                }
            }

            JsonValue value(size_t depth)
            {
                if (depth > 128)
                    error();
                whitespace();
                if (p == s.size())
                    error();
                JsonValue v;
                char c = s[p];
                if (c == '"')
                {
                    v.kind = JsonValue::Kind::String;
                    v.text = string();
                    return v;
                }
                if (c == '{' || c == '[')
                {
                    ++p;
                    bool obj = c == '{';
                    v.kind = obj ? JsonValue::Kind::Object : JsonValue::Kind::Array;
                    if (consume(obj ? '}' : ']'))
                        return v;
                    for (;;)
                    {
                        if (obj)
                        {
                            whitespace();
                            auto key = string();
                            if (!consume(':'))
                                error();
                            if (!v.object.emplace(key, value(depth + 1)).second)
                                error();
                        }
                        else
                            v.array.push_back(value(depth + 1));
                        if (consume(obj ? '}' : ']'))
                            break;
                        if (!consume(','))
                            error();
                    }
                    return v;
                }
                for (const auto &lit :
                     {std::string("null"), std::string("true"), std::string("false")})
                    if (s.compare(p, lit.size(), lit) == 0)
                    {
                        p += lit.size();
                        if (lit != "null")
                        {
                            v.kind = JsonValue::Kind::Bool;
                            v.boolean = lit == "true";
                        }
                        return v;
                    }
                size_t start = p;
                if (s[p] == '-')
                    ++p;
                if (p == s.size())
                    error();
                if (s[p] == '0')
                    ++p;
                else
                {
                    if (s[p] < '1' || s[p] > '9')
                        error();
                    while (p < s.size() && s[p] >= '0' && s[p] <= '9')
                        ++p;
                }
                if (p < s.size() && s[p] == '.')
                {
                    ++p;
                    size_t digit = p;
                    while (p < s.size() && s[p] >= '0' && s[p] <= '9')
                        ++p;
                    if (p == digit)
                        error();
                }
                if (p < s.size() && (s[p] == 'e' || s[p] == 'E'))
                {
                    ++p;
                    if (p < s.size() && (s[p] == '+' || s[p] == '-'))
                        ++p;
                    size_t digit = p;
                    while (p < s.size() && s[p] >= '0' && s[p] <= '9')
                        ++p;
                    if (p == digit)
                        error();
                }
                v.kind = JsonValue::Kind::Number;
                v.text = s.substr(start, p - start);
                v.number();
                return v;
            }

          public:
            explicit Parser(const std::string &text) : s(text)
            {
            }

            JsonValue parse()
            {
                auto v = value(0);
                whitespace();
                if (p != s.size())
                    error();
                return v;
            }
        };

        Detection detection(const JsonValue &v)
        {
            Detection d{};
            d.category = MarkCategory(v.at("category").i64());
            d.quality_flags = uint32_t(v.at("quality_flags").u64());
            auto points = v.at("corners").array;
            if (points.size() != 4)
                throw std::runtime_error("expected four corners");
            for (size_t i = 0; i < 4; ++i)
            {
                if (points[i].array.size() != 2)
                    throw std::runtime_error("point arity");
                d.corners[i] = {float(points[i].array[0].number()),
                                float(points[i].array[1].number())};
            }
            auto b = v.at("bbox").array;
            if (b.size() != 4)
                throw std::runtime_error("bbox arity");
            d.bbox = {float(b[0].number()), float(b[1].number()), float(b[2].number()),
                      float(b[3].number())};
            if (!v.at("orientation").null())
            {
                auto o = v.at("orientation").array;
                if (o.size() != 4)
                    throw std::runtime_error("orientation arity");
                std::array<int, 4> a{};
                for (size_t i = 0; i < 4; ++i)
                    a[i] = int(o[i].i64());
                d.attributes.orientation = a;
            }
            if (!v.at("confidence").null())
                d.confidence = float(v.at("confidence").number());
            if (!v.at("marker_code").null())
            {
                const auto &m = v.at("marker_code");
                d.attributes.marker_code =
                    MarkerCode{m.at("scheme").text, int(m.at("value").i64())};
            }
            return d;
        }
    }

    JsonValue readJson(const std::string &text)
    {
        return Parser(text).parse();
    }

    FrameResult readResult(const JsonValue &v, uint64_t id, int64_t time)
    {
        FrameResult r{};
        r.frame_id = id;
        r.timestamp_us = time;
        r.status = Status(v.at("status").i64());
        for (const auto &d : v.at("detections").array)
            r.detections.push_back(detection(d));
        for (const auto &t : v.at("tracks").array)
            r.tracks.push_back({size_t(t.at("detection_index").u64()), detection(t.at("result"))});
        const auto &disp = v.at("display");
        if (!disp.null())
            r.display_state = DisplayState{disp.at("source_frame_id").u64(), disp.at("age").u64(),
                                           disp.at("held").boolean, disp.at("value").text};
        for (const auto &d : v.at("diagnostics").array)
            r.diagnostics.push_back(d.text);
        return r;
    }

    namespace
    {
        void requireCorner(bool ok, const char *message)
        {
            if (!ok)
                throw std::runtime_error(message);
        }

        const std::vector<JsonValue> &cornerArray(const JsonValue &value)
        {
            requireCorner(value.kind == JsonValue::Kind::Array, "expected corner evidence array");
            return value.array;
        }

        int cornerInteger(const JsonValue &value)
        {
            const auto number = value.i64();
            requireCorner(number >= std::numeric_limits<int>::min() &&
                              number <= std::numeric_limits<int>::max(), "corner integer overflow");
            return int(number);
        }

        cv::Point2d cornerPoint(const JsonValue &value)
        {
            const auto &coordinates = cornerArray(value);
            requireCorner(coordinates.size() == 2, "corner point arity");
            return {coordinates[0].number(), coordinates[1].number()};
        }

        std::vector<cv::Point2d> cornerPoints(const JsonValue &value)
        {
            std::vector<cv::Point2d> points;
            for (const auto &point : cornerArray(value))
                points.push_back(cornerPoint(point));
            return points;
        }
    }

    // V03：协议只恢复完整数值和本帧身份；拟合、残差、连续弧仍交独立 validator 重算。
    CornerMeasurement readCornerMeasurement(const JsonValue &v, uint64_t frame,
                                        const std::set<size_t> &components)
    {
        auto &es = cornerArray(v);
        requireCorner(es.size() == 4, "measurement must contain four evidence entries");
        mark::CornerMeasurement m;
        std::set<int> physical;
        for (auto &data : es)
        {
            // 无版本的旧记录未保存线模型，不能从原始弧猜测当时采用的估计器。
            const auto version = data.object.find("evidence_schema_version");
            requireCorner(version != data.object.end(),
                          "UNREPLAYABLE_CORNER_EVIDENCE: missing line-model schema");
            requireCorner(cornerInteger(version->second) == corner_evidence_schema_version,
                          "unsupported corner evidence schema");
            int id = cornerInteger(data.at("physical"));
            requireCorner(id >= 0 && id < 4 && physical.insert(id).second, "invalid physical identity");
            auto &e = m.evidence_[id];
            e.physical_corner_ = static_cast<mark::PhysicalCorner>(id);
            e.frame_id_ = data.at("frame_id").u64();
            e.component_id_ = data.at("component_id").u64();
            requireCorner(e.frame_id_ == frame && components.count(e.component_id_),
                    "noncurrent evidence reference");
            requireCorner(data.at("evidence_id").kind == JsonValue::Kind::String,
                          "expected evidence ID string");
            e.stable_id_ = data.at("evidence_id").text;
            requireCorner(!e.stable_id_.empty(), "empty evidence ID");
            e.model_vertex_id_ = cornerInteger(data.at("model_vertex_id"));
            auto &model = cornerArray(data.at("model_edge_ids"));
            auto &starts = cornerArray(data.at("observed_segment_ids"));
            auto &ends = cornerArray(data.at("observed_segment_end_ids"));
            auto &lines = cornerArray(data.at("fitted_lines"));
            auto &segments = cornerArray(data.at("finite_segments"));
            auto &arcs = cornerArray(data.at("support_arcs"));
            auto &mean = cornerArray(data.at("line_mean_residual_px"));
            auto &max = cornerArray(data.at("line_max_residual_px"));
            auto &extension = cornerArray(data.at("support_extension_px"));
            auto &kinds = cornerArray(data.at("line_model_kinds"));
            auto &axes = cornerArray(data.at("periodic_axes"));
            auto &coefficients = cornerArray(data.at("periodic_coefficients"));
            auto &model_mean = cornerArray(data.at("model_mean_residual_px"));
            auto &model_max = cornerArray(data.at("model_max_residual_px"));
            for (auto *pair :
                 {&model, &starts, &ends, &lines, &segments, &arcs, &mean, &max, &extension,
                  &kinds, &axes, &coefficients, &model_mean, &model_max})
                requireCorner(pair->size() == 2, "two-edge field arity");
            for (size_t i = 0; i < 2; ++i)
            {
                e.model_edge_ids_[i] = cornerInteger(model[i]);
                e.observed_segment_ids_.push_back(cornerInteger(starts[i]));
                e.observed_segment_end_ids_[i] = cornerInteger(ends[i]);
                auto &l = cornerArray(lines[i]);
                requireCorner(l.size() == 4, "line arity");
                for (size_t k = 0; k < 4; ++k)
                    (i ? e.line_b_ : e.line_a_)[k] = l[k].number();
                auto seg = cornerPoints(segments[i]);
                requireCorner(seg.size() == 2, "segment arity");
                for (size_t k = 0; k < 2; ++k)
                    (i ? e.edge_segment_b_ : e.edge_segment_a_)[k] = seg[k];
                e.original_support_arcs_[i] = cornerPoints(arcs[i]);
                e.line_mean_residual_px_[i] = mean[i].number();
                e.line_max_residual_px_[i] = max[i].number();
                e.support_extension_px_[i] = extension[i].number();
                const int kind = cornerInteger(kinds[i]);
                requireCorner(kind == int(LineFitModel::Legacy) ||
                                  kind == int(LineFitModel::PeriodicLeastSquares) ||
                                  kind == int(LineFitModel::PeriodicPhaseMedian) ||
                                  kind == int(LineFitModel::PeriodicL1) ||
                                  kind == int(LineFitModel::PeriodicZeroSlope),
                              "unknown corner line model");
                e.line_model_kinds_[i] = static_cast<LineFitModel>(kind);
                e.periodic_axes_[i] = cornerInteger(axes[i]);
                requireCorner(e.periodic_axes_[i] == 0 || e.periodic_axes_[i] == 1,
                              "invalid periodic axis");
                e.periodic_coefficients_[i] = coefficients[i].number();
                e.model_mean_residual_px_[i] = model_mean[i].number();
                e.model_max_residual_px_[i] = model_max[i].number();
            }
            e.original_turn_arc_ = cornerPoints(data.at("turn_arc"));
            requireCorner(data.at("original_observation").kind == JsonValue::Kind::Bool &&
                              data.at("truncated").kind == JsonValue::Kind::Bool,
                          "expected corner observation flags");
            e.original_observation_ = data.at("original_observation").boolean;
            e.truncated_ = data.at("truncated").boolean;
            e.corner_error_px_ = data.at("corner_error_px").number();
            e.error_ = data.at("measurement_error_px").number();
            e.intersection_ = cornerPoint(data.at("intersection"));
            m.physical_corners_[id] = e.intersection_;
        }
        return m;
    }

}
