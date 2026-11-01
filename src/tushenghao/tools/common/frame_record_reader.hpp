#pragma once
#include "mark/detector_types.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mark
{
    struct CornerMeasurement;
}

namespace mark::validation
{
    // 受控JSON读取只链接工具/测试；number保留token，64位整数不经double中转。
    struct JsonValue
    {
        enum class Kind
        {
            Null,
            Bool,
            Number,
            String,
            Array,
            Object
        };
        Kind kind{Kind::Null};
        std::string text;
        bool boolean{false};
        std::vector<JsonValue> array;
        std::map<std::string, JsonValue> object;
        const JsonValue &at(const std::string &) const;

        bool null() const
        {
            return kind == Kind::Null;
        }

        double number() const;
        uint64_t u64() const;
        int64_t i64() const;
        bool operator==(const JsonValue &) const;
    };

    JsonValue readJson(const std::string &);
    FrameResult readResult(const JsonValue &, uint64_t, int64_t);
    // V03：完整协议→角测量；只核身份/字段，几何与线模型仍由独立 validator 重算。
    CornerMeasurement readCornerMeasurement(const JsonValue &, uint64_t,
                                          const std::set<size_t> &);
}
