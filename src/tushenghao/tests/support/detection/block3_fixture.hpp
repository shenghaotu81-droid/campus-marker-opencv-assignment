// 仅测试用：固定已核对模型渲染成真实BGR画面，再经真实preprocess/工作图分割。
// 隔离拟合时用渲染真值四边形；端到端匹配另测，不传模型变换。
#pragma once
#include "preprocess/preprocess.hpp"
#include "vision/types/detection/corner_types.hpp"
#include "corners/measurement/corner_resolver.hpp"
#include "corners/observation/corner_observation.hpp"
#include "vision/types/marker_geometry.hpp"
#include <opencv2/imgproc.hpp>
#include <filesystem>

namespace fixture
{
    // 显式测试预算只用于固定断言，不代表G3/G4批准数值，严禁写回生产YAML。
    inline mark::CornerConfig cornerConfig()
    {
        mark::CornerConfig c;
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
        cv::FileStorage fs((std::filesystem::path(VISION_SOURCE_ROOT) / "config/marker_geometry.yaml").string(), cv::FileStorage::READ);
        if (!fs.isOpened())
            throw std::runtime_error("fixture model missing");
        Model model;
        for (const auto &p:fs["polygons"])
        {
            Polygon polygon;
            for (const auto &v:p["vertices"])
                polygon.vertices.push_back({float(v[0]), float(v[1])});
            model.polygons.push_back(polygon);
        }
        return model;
    }

    struct Scene
    {
        mark::PreparedFrame frame;
        mark::CornerPoints quad;
        Model geometry;
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
        std::vector<std::vector<cv::Point> > polygons;
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
        const std::array<cv::Point2d, 4> model_corners{{{0, 0}, {80, 0}, {80, 80}, {0, 80}}};
        for (size_t i = 0; i < 4; ++i)
            s.quad[i] = transform(model_corners[i]);
        return s;
    }

}

namespace fixture
{
    inline mark::CornerResolution resolve(const Scene &s, const mark::CornerConfig &c)
    {
        mark::OriginalContourIndex index(s.frame);
        (void)c;
        return mark::resolveObservedCorners(s.quad, index);
    }

}
