// 把工作图转灰度、阈值分割，输出轮廓、边框和边界截断事实供结构匹配。
#include "geometry/geometry_observation.hpp"
#include <opencv2/imgproc.hpp>

namespace mark::detector
{
    std::vector<WhiteComponent> extract_white_components(const PreparedFrame &frame,
                                                        const GeometryConfig &config)
    {
        cv::Mat gray, binary;
        cv::cvtColor(frame.image_, gray, cv::COLOR_BGR2GRAY);
        cv::threshold(gray, binary, config.white_threshold_, 255, cv::THRESH_BINARY);
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        std::vector<WhiteComponent> components;
        for (const auto &contour : contours)
        {
            WhiteComponent component;
            component.contour_ = contour;
            component.bounding_box_ = cv::boundingRect(contour);
            // 接触工作图边缘的白片可能被裁切，不能充当完整角片。
            const auto &box = component.bounding_box_;
            component.touches_border_ = box.x <= 0 || box.y <= 0 ||
                                        box.br().x >= frame.image_.cols || box.br().y >= frame.image_.rows;
            components.push_back(component);
        }
        return components;
    }
}
