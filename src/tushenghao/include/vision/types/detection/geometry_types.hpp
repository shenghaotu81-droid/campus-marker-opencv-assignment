#pragma once
#include <vector>
#include <opencv2/core.hpp>
namespace mark {
// 当前工作图实测白片；不携带模型身份、变换或跨帧状态。
struct WhiteComponent {
    std::vector<cv::Point> contour_;
    cv::Rect bounding_box_;
    bool touches_border_{false};
};
}
