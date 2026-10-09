// 输入只读原图和明确assignment组件；输出完整原图连续轮廓，失败有原因。
#pragma once
#include "core/prepared_frame.hpp"
#include "mark/detector_config.hpp"
#include <string>
#include <map>
namespace mark {
struct OriginalContourResult {
    std::vector<cv::Point> contour;
    std::string reason;
};
// 沙盒性能实验：该索引只活在一次decode调用中；不使用static或跨帧缓存。
struct OriginalContourIndex {
    bool ready=false;
    double parity_shift=0;
    std::vector<std::vector<cv::Point>> contours;
    std::map<size_t,OriginalContourResult> matched;
};
OriginalContourResult observeOriginalContour(const PreparedFrame&, const WhiteComponent&,
                                            const CornerObservationBudget&, OriginalContourIndex&);
OriginalContourResult observeOriginalContour(const PreparedFrame&, const WhiteComponent&,
                                            const CornerObservationBudget&);
}
