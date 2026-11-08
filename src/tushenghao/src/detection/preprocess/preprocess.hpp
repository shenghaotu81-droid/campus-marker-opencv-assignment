#pragma once

#include "vision/config/detector_config.hpp"
#include "vision/types/detector_types.hpp"
#include "vision/types/prepared_frame_types.hpp"

namespace mark::detector
{

    // 输入原帧，输出只读原图视图、resize 后 BGR8 与像素中心映射；二值化属于后续观察。
    PreparedFrame preprocess(
        const FrameInput &frame,
        const PreprocessConfig &config);

} // namespace mark::detector
