// 历史工具可转调唯一Block5 serializer；不保留第二份JSON实现，旧证据不重写。
#pragma once
#include "pipeline/diagnostics_recorder.hpp"

namespace audit
{
    inline std::string frameRecord(const mark::FrameRecord &record)
    {
        return mark::serializeFrameRecord(record);
    }

    inline std::string quote(const std::string &value)
    {
        return mark::quoteJson(value);
    }
}
