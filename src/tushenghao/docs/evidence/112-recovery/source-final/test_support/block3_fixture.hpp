// 仅测试用：固定已核对模型渲染成真实BGR画面，再经真实preprocess/工作图分割。
// assignment和affine由fixture显式给定，用于隔离原图定位；不传给正式检测。
#pragma once
#include "preprocess/preprocess.hpp"
#include "corners/corner_types.hpp"
#include "core/marker_geometry.hpp"
#include <opencv2/imgproc.hpp>
#include <filesystem>

namespace fixture
{
    // 显式测试预算只用于固定断言，不代表G3/G4批准数值，严禁写回生产YAML。
    inline mark::CornerConfig cornerConfig()
    {
        mark::CornerConfig c;
        c.min_line_points_ = 3;
        c.max_line_fit_error_ = 1;
        c.max_corner_error_ = 5;
        c.approximation_epsilon_ = 1;
        c.min_intersection_angle_deg_ = 5;
        c.observation_budget_ = mark::CornerObservationBudget{10, 4, 5, 2, 12, 6, 5};
        return c;
    }

    inline mark::MarkerGeometry model()
    {
        return mark::loadMarkerGeometry(
            std::filesystem::path(__FILE__).parent_path().parent_path() /
            "config/marker_geometry.yaml");
    }

    struct Scene
    {
        mark::PreparedFrame frame;
        mark::GeometryHypothesis hypothesis;
        mark::MarkerGeometry geometry;
    };

    // 非连续ID且打乱容器，验证ID查找而不是隐式数组下标。
    inline Scene scene(int work_width = 1440, int work_height = 1080, double angle = 0)
    {
        Scene s;
        s.geometry = model();
        mark::FrameInput input{};
        input.frame_id = 7;
        input.image = cv::Mat(1080, 1440, CV_8UC3, cv::Scalar(0, 0, 0));
        const double rad = angle * CV_PI / 180, cs = std::cos(rad), sn = std::sin(rad);
        auto transform = [&](cv::Point2d p)
        {
            p = (p - cv::Point2d(40, 40)) * 2;
            return cv::Point2d(1000 + cs * p.x - sn * p.y, 750 + sn * p.x + cs * p.y);
        };
        std::vector<std::vector<cv::Point>> polygons;
        for (const auto &part : s.geometry.polygons)
        {
            std::vector<cv::Point> polygon;
            for (auto p : part.vertices)
                polygon.emplace_back(transform(p));
            polygons.push_back(polygon);
        }
        cv::fillPoly(input.image, polygons, cv::Scalar(255, 255, 255));
        mark::PreprocessConfig pre{};
        pre.work_width = work_width;
        pre.work_height = work_height;
        pre.threshold = 200;
        s.frame = mark::preprocess(input, pre);
        s.hypothesis.affine_transform_ =
            (cv::Mat_<double>(2, 3) << 2 * cs * s.frame.scale_x_, -2 * sn * s.frame.scale_x_,
             (1000 - 80 * cs + 80 * sn + 0.5) * s.frame.scale_x_ - 0.5, 2 * sn * s.frame.scale_y_,
             2 * cs * s.frame.scale_y_, (750 - 80 * sn - 80 * cs + 0.5) * s.frame.scale_y_ - 0.5);
        // 对每个已知白片独立栅格化工作图，身份仅在测试侧建立；正式检测不访问fixture。
        for (size_t i = 0; i < polygons.size(); ++i)
        {
            cv::Mat one = cv::Mat::zeros(input.image.size(), CV_8UC1), work;
            cv::fillPoly(one, std::vector<std::vector<cv::Point>>{polygons[i]}, cv::Scalar(255));
            cv::resize(one, work, s.frame.image_.size());
            cv::threshold(work, work, 200, 255, cv::THRESH_BINARY);
            std::vector<std::vector<cv::Point>> contours;
            cv::findContours(work, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
            mark::WhiteComponent c{};
            c.component_id_ = 100 + i * 7;
            c.contour_ = contours.at(0);
            c.area_ = cv::contourArea(c.contour_);
            c.bounding_box_ = cv::boundingRect(c.contour_);
            s.frame.components_.insert(s.frame.components_.begin(), c);
            s.hypothesis.assignments_.push_back({s.geometry.polygons[i].id, c.component_id_});
        }
        return s;
    }
}
