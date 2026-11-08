#pragma once
#include "vision/types/prepared_frame_types.hpp"
#include "vision/config/detector_config.hpp"
#include <string>
namespace mark::detector
{
    struct OriginalContourResult
    {
        const std::vector<cv::Point> *contour_{nullptr};
        size_t component_id_{std::numeric_limits<size_t>::max()};
        std::string reason_;
    };
// 一帧一索引，直接按四边形角查当前原图轮廓；不缓存跨帧、不访问部件模型。
    class OriginalContourIndex
    {
public:
        explicit OriginalContourIndex(const PreparedFrame &);
        OriginalContourResult at_corner(cv::Point2d);
private:
        void prepare();
        const PreparedFrame &frame_;
        bool prepared_{false};
        std::vector<std::vector<cv::Point> > contours_;
    };
}
