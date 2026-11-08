// 仅测试用：固定已核对模型渲染成真实BGR画面，再经真实preprocess/工作图分割。
// 隔离拟合时用渲染真值四边形；端到端匹配另测，不传模型变换。
#pragma once
#include "preprocess/preprocess.hpp"
#include "vision/types/corner_types.hpp"
#include "pipeline/decode_stage.hpp"
#include "corners/corner_observation.hpp"
#include <opencv2/imgproc.hpp>
#include <filesystem>

namespace fixture
{
    // 显式测试预算只用于固定断言，不代表下游控制/Tracking批准数值，严禁写回生产YAML。
    inline mark::detector::CornerConfig corner_config()
    {
        mark::detector::CornerConfig c;
        return c;
    }

    struct Polygon
    {
        std::vector<cv::Point2f> vertices;
    };
    struct Model
    {
        std::vector<Polygon> polygons;
    };
    inline Model model()
    {
        Model model{{
            {{{0,0}, {30,0}, {30,8}, {8,8}, {8,30}, {0,30}}},
            {{{80,80}, {50,80}, {50,72}, {72,72}, {72,50}, {80,50}}},
            {{{0,80}, {30,80}, {30,72}, {8,72}, {8,50}, {0,50}}},
            {{{66,0}, {80,0}, {80,14}, {72,14}, {72,8}, {66,8}}},
            {{{50,0}, {58,0}, {58,8}, {50,8}}},
            {{{72,22}, {80,22}, {80,30}, {72,30}}}
        }};
        return model;
    }

    struct Scene
    {
        mark::detector::PreparedFrame frame;
        mark::detector::CornerPoints quad;
        Model geometry;
    };

    // 非连续ID且打乱容器，验证ID查找而不是隐式数组下标。
    inline Scene scene(int work_width_ = 1440, int work_height_ = 1080, double angle = 0)
    {
        Scene s;
        s.geometry = model();
        mark::detector::FrameInput input{};
        input.frame_id_ = 7;
        input.image_ = cv::Mat(1080, 1440, CV_8UC3, cv::Scalar(0, 0, 0));
        const double rad = angle * CV_PI / 180, cs = std::cos(rad), sn = std::sin(rad);
        auto transform = [&](cv::Point2d p)
                         {
                             p = (p - cv::Point2d(40, 40)) * 2;
                             return cv::Point2d(1000 + cs * p.x - sn * p.y, 750 + sn * p.x + cs * p.y);
                         };
        std::vector<std::vector<cv::Point> > polygons;
        for (const auto &part : s.geometry.polygons)
        {
            std::vector<cv::Point> polygon;
            for (auto p : part.vertices)
                polygon.emplace_back(transform(p));
            polygons.push_back(polygon);
        }
        cv::fillPoly(input.image_, polygons, cv::Scalar(255, 255, 255));
        mark::detector::PreprocessConfig pre{};
        pre.work_width_ = work_width_;
        pre.work_height_ = work_height_;
        pre.threshold_ = 200;
        s.frame = mark::detector::preprocess(input, pre);
        const std::array<cv::Point2d, 4> model_corners{{{0, 0}, {80, 0}, {80, 80}, {0, 80}}};
        for (size_t i = 0; i < 4; ++i)
            s.quad[i] = transform(model_corners[i]);
        return s;
    }

}

namespace fixture
{
    inline mark::detector::CornerResolution resolve(const Scene &s, const mark::detector::CornerConfig &c)
    {
        mark::detector::OriginalContourIndex index(s.frame);
        (void)c;
        return mark::detector::resolve_observed_corners(s.quad, index);
    }

}
