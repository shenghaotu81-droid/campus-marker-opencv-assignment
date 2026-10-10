#include "common/frame_record_reader.hpp"
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
}
