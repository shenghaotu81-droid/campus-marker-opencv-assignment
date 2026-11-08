// Q03/P03：相同 Tag 数值契约，输入配置，输出原校验结论；原生对象由检测步骤拥有。
#pragma once

namespace mark::pose
{
    struct TagConfig;
}

namespace mark::pose::detail
{
    bool validateTagConfig(const TagConfig &);
}
