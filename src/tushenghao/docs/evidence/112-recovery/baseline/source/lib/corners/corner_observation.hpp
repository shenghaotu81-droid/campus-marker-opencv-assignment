// 输入只读原图和明确assignment组件；输出完整原图连续轮廓，失败有原因。
#pragma once
#include "core/prepared_frame.hpp"
#include "mark/detector_config.hpp"
#include <string>
namespace mark {
struct OriginalContourResult {
    std::vector<cv::Point> contour;
    std::string reason;
};
OriginalContourResult observeOriginalContour(const PreparedFrame&, const WhiteComponent&,
                                            const CornerObservationBudget&);
}
