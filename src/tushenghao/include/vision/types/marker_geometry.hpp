// 把 MARK 灯的“图纸”翻译成程序能理解的数据结构(图纸模型层建模)
#pragma once

#include <array>


namespace mark
{
    // G2图纸取点的固定身份；检测直接接四边形，不再消费部件边号。
    struct ModelCornerBinding
    {
        const char *part;
        int vertex;
    };
    using ModelCornerContract = std::array<ModelCornerBinding, 4>;
    inline constexpr ModelCornerContract model_corner_contract{{
        {"L0", 0}, {"M1", 1}, {"L2", 0}, {"L3", 0}}};

}
