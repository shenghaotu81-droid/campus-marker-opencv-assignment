#pragma once

#include "mark/detector_config.hpp"
#include "mark/detector_types.hpp"
#include "core/prepared_frame.hpp"

namespace mark
{

    // Block1 preprocess:
    // FrameInput -> PreparedFrame
    //
    // 职责：
    // 1. resize 到工作尺寸
    // 2. threshold 生成工作二值图
    // 3. 保存原图到工作图坐标映射
    // 4. 保存帧上下文
    PreparedFrame preprocess(
        const FrameInput &frame,
        const PreprocessConfig &config);

} // namespace mark