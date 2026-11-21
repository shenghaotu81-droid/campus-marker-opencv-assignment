// 从工作图分割白片，只提取外轮廓。
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

        // 粗匹配只需要外轮廓的折线端点。
        cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        std::vector<WhiteComponent> components;
        for (const auto &contour : contours)
        {
            WhiteComponent component;
            component.contour_ = contour;

            // 保存轴对齐外接矩形。右、下坐标为开边界。
            component.bounding_box_ = cv::boundingRect(contour);

            const auto &box = component.bounding_box_;

            // 接触工作图边缘的白片可能不完整。
            component.touches_border_ = box.x <= 0 || box.y <= 0 ||
                                        box.br().x >= frame.image_.cols ||
                                        box.br().y >= frame.image_.rows;
            components.push_back(component);
        }
        return components;
    }
}
