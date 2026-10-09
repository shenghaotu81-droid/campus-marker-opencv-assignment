// 原视频没有亚像素真值；按已封存的种子/采样顺序重建独立图像，验证定位与缺角拒绝。
#include "block3_fixture.hpp"
#include "config/config.hpp"
#include "corners/corner_resolver.hpp"
#include "corners/detection_validator.hpp"
#include "core/observed_geometry_utils.hpp"
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>

namespace
{
    struct Sample
    {
        int id, amplitude, local, dx, dy, negative;
        double angle;
    };

    fixture::Scene render(const Sample &sample)
    {
        auto scene = fixture::scene(1440, 1080, sample.angle);
        scene.hypothesis.affine_transform_.at<double>(0, 2) += sample.dx;
        scene.hypothesis.affine_transform_.at<double>(1, 2) += sample.dy;
        const cv::Mat translation = (cv::Mat_<double>(2, 3) << 1, 0, sample.dx, 0, 1, sample.dy);
        cv::Mat shifted;
        cv::warpAffine(scene.frame.original_image_, shifted, translation,
                       scene.frame.original_image_.size(), cv::INTER_NEAREST, cv::BORDER_CONSTANT);
        for (auto &component : scene.frame.components_)
            for (auto &point : component.contour_)
                point += cv::Point(sample.dx, sample.dy);
        scene.frame.original_image_ = shifted.clone();
        if (sample.amplitude)
        {
            // 奇偶位移的均值为零；局部模式左右反向，工作图绑定保持已知输入以隔离角解析。
            scene.frame.original_image_.setTo(cv::Scalar(0, 0, 0));
            for (int y = 0; y < shifted.rows; ++y)
                for (int x = 0; x < shifted.cols; ++x)
                {
                    const int phase = y % 2 ? 1 : -1;
                    const int sign = sample.local && x < 1000 + sample.dx ? -1 : 1;
                    const int destination = x + sample.amplitude * phase * sign;
                    if (destination >= 0 && destination < shifted.cols)
                        scene.frame.original_image_.at<cv::Vec3b>(y, destination) =
                            shifted.at<cv::Vec3b>(y, x);
                }
        }
        return scene;
    }

    std::array<cv::Point2d, 4> truth(const fixture::Scene &scene)
    {
        const std::array<std::string, 4> parts{{"L0", "M1", "L2", "L3"}};
        std::array<cv::Point2d, 4> corners{};
        for (size_t k = 0; k < parts.size(); ++k)
            for (const auto &polygon : scene.geometry.polygons)
                if (polygon.id == parts[k])
                    corners[k] = scene.frame.workToOriginal(mark::observed::project(
                        scene.hypothesis.affine_transform_, polygon.vertices[k == 1 ? 1 : 0]));
        return corners;
    }
}

int main(int argc, char **argv)
{
    try
    {
        if (argc != 2 || (std::string(argv[1]) != "test" && std::string(argv[1]) != "calibration"))
            throw std::invalid_argument("usage: recovery112_synthetic calibration|test");
        cv::setNumThreads(1);
        const bool test = std::string(argv[1]) == "test";
        const auto config = mark::loadConfig("src/tushenghao/config/detector_verification.yaml")
                                .detector_config.corner_;
        std::mt19937 generator(test ? 7919 : 112);
        std::uniform_real_distribution<double> angles(-6, 6);
        std::uniform_int_distribution<int> shifts(-15, 15);
        std::cout << std::setprecision(17);
        int accepted = 0, positives = 0, negatives = 0, false_positive = 0, tamper_failures = 0;
        double maximum_error = 0;
        for (int id = 0; id < (test ? 120 : 36); ++id)
        {
            // 先消耗 angle/dx/dy 的随机数，再筛样本；顺序决定与封存样本逐项相同。
            Sample sample{id, id % 3, (id / 3) % 2, 0, 0, 0, angles(generator)};
            sample.dx = shifts(generator);
            sample.dy = shifts(generator);
            if (test && id >= 100)
            {
                sample.amplitude = (id - 100) % 2;
                sample.negative = 1 + (id - 100) % 2;
            }
            if (sample.amplitude == 2 && id != 2)
                continue;
            auto scene = render(sample);
            const auto expected = truth(scene);
            if (sample.negative == 1)
                scene.frame.original_image_.setTo(cv::Scalar(0, 0, 0));
            if (sample.negative == 2)
                cv::circle(scene.frame.original_image_, cv::Point(expected[1]), 15,
                           cv::Scalar(0, 0, 0), cv::FILLED);
            const auto resolved =
                mark::resolveObservedCorners(scene.frame, scene.hypothesis, scene.geometry, config);
            bool valid = false, tamper_rejected = true;
            int modeled = 0;
            double error = -1;
            if (resolved.measurement_)
            {
                const auto &measurement = *resolved.measurement_;
                const auto order = mark::orderScreenCorners(measurement.physical_corners_, config);
                if (order.screen_order_)
                    valid =
                        mark::validateDetectionGeometry(measurement, *order.screen_order_,
                                                        scene.frame.original_image_.size(), config)
                            .valid_;
                error = 0;
                for (size_t corner = 0; corner < 4; ++corner)
                {
                    error = std::max(
                        error, cv::norm(measurement.physical_corners_[corner] - expected[corner]));
                    for (auto kind : measurement.evidence_[corner].line_model_kinds_)
                        modeled += kind != mark::LineFitModel::Legacy;
                }
                if (valid && modeled)
                {
                    // 只计实际使用周期证据的篡改：c 加 0.25，完整生产 validator 必须拒绝。
                    auto modified = measurement;
                    bool changed = false;
                    for (auto &evidence : modified.evidence_)
                        for (size_t edge = 0; edge < 2; ++edge)
                            if (!changed &&
                                evidence.line_model_kinds_[edge] != mark::LineFitModel::Legacy)
                            {
                                evidence.periodic_coefficients_[edge] += 0.25;
                                changed = true;
                            }
                    tamper_rejected =
                        !mark::validateDetectionGeometry(modified, *order.screen_order_,
                                                         scene.frame.original_image_.size(), config)
                             .valid_;
                    tamper_failures += !tamper_rejected;
                }
            }
            if (sample.negative)
            {
                ++negatives;
                false_positive += valid;
            }
            else
            {
                ++positives;
                accepted += valid;
                if (valid)
                    maximum_error = std::max(maximum_error, error);
            }
            std::cout << "{\"id\":" << id << ",\"split\":" << std::quoted(argv[1])
                      << ",\"amplitude\":" << sample.amplitude << ",\"local\":" << sample.local
                      << ",\"angle\":" << sample.angle << ",\"dx\":" << sample.dx
                      << ",\"dy\":" << sample.dy << ",\"negative\":" << sample.negative
                      << ",\"measurement\":" << bool(resolved.measurement_)
                      << ",\"valid\":" << valid << ",\"max_truth_error\":" << error
                      << ",\"modeled_edges\":" << modeled
                      << ",\"tamper_rejected\":" << tamper_rejected
                      << ",\"reason\":" << std::quoted(resolved.rejection_reason_) << "}\n";
        }
        std::cerr << "positive=" << accepted << '/' << positives << " negatives=" << negatives
                  << " false_positive=" << false_positive << " max_error=" << maximum_error
                  << " tamper_failures=" << tamper_failures << '\n';
        return accepted == (test ? 67 : 24) && positives == (test ? 68 : 25) &&
                       negatives == (test ? 20 : 0) && !false_positive && maximum_error < 2 &&
                       !tamper_failures
                   ? 0
                   : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
