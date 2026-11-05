#pragma once
#include "vision/types/detection/prepared_image.hpp"
#include "vision/config/detector_config.hpp"
#include <string>
namespace mark {
struct OriginalContourResult {
    std::vector<cv::Point> contour;
    size_t component_id{std::numeric_limits<size_t>::max()};
    std::string reason;
};
// 一帧一索引，直接按四边形角查当前原图轮廓；不缓存跨帧、不访问部件模型。
class OriginalContourIndex {
public:
    explicit OriginalContourIndex(const PreparedFrame&);
    uint64_t frameId() const;
    OriginalContourResult atCorner(cv::Point2d);
private:
    void prepare();
    const PreparedFrame &frame_;
    const uint64_t frame_id_;
    bool prepared_{false};
    std::vector<std::vector<cv::Point>> contours_;
};
}
