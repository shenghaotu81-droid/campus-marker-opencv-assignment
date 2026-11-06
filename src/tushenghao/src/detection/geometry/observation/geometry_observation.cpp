#include "geometry/observation/geometry_observation.hpp"
#include <opencv2/imgproc.hpp>
namespace mark
{
    std::vector<WhiteComponent> extractWhiteComponents(
        const PreparedFrame &frame,
        const GeometryConfig &config)
    {
        std::vector<WhiteComponent> components;

        // BGR 图像不能直接 threshold。
        // 先转换灰度，使后续二值分割与输入格式解耦。
        cv::Mat gray_image;

        cv::cvtColor(
            frame.image_,
            gray_image,
            cv::COLOR_BGR2GRAY);

        cv::Mat binary_image;

        // 根据配置中的白色阈值提取候选白区域。
        cv::threshold(
            gray_image,
            binary_image,
            config.white_threshold_,
            255,
            cv::THRESH_BINARY);

        std::vector<std::vector<cv::Point> > contours;

        cv::findContours(
            binary_image,
            contours,
            cv::RETR_EXTERNAL,
            cv::CHAIN_APPROX_SIMPLE);


        for (const auto &contour : contours)
        {


            WhiteComponent component;


            component.contour_ = contour;


            component.bounding_box_ =
                cv::boundingRect(contour);

            const cv::Rect image_rect(
                0,
                0,
                frame.image_.cols,
                frame.image_.rows);

            // 判断白块是否接触工作图边缘。
            // 边界接触意味着该结构可能被裁切，
            // 不能作为完整角片进入匹配。
            component.touches_border_ =
                (component.bounding_box_.x <= image_rect.x) ||
                (component.bounding_box_.y <= image_rect.y) ||
                (component.bounding_box_.br().x >= image_rect.width) ||
                (component.bounding_box_.br().y >= image_rect.height);

            components.push_back(component);
        }

        return components;
    }

}
