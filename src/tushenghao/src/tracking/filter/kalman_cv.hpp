#pragma once
#include "vision/math/covariance_validation.hpp"
#include "vision/config/tracking_config.hpp"
namespace mark::tracking::detail
{
    struct InnovationResult
    {
        cv::Vec3d residual;
        cv::Matx33d S, R;
        double score{};
    };
    class CvFilter
    {
public:
        void initialize(const cv::Vec3d &, const cv::Matx33d &, double);
        void bootstrap(const cv::Vec3d &, const cv::Matx33d &, const cv::Vec3d &, const cv::Matx33d &, double, double, double = 0, double = 0);
        void predict(double, double, double = 0);
        InnovationResult innovation(const cv::Vec3d &, const cv::Matx33d &) const;
        void correct(const InnovationResult &);
        cv::Vec3d position() const;
        cv::Vec3d velocity() const;
        const Mat6 &covariance() const noexcept
        {
            return P_;
        }

private: Vec6 x_{Vec6::all(0)};
        Mat6 P_{Mat6::zeros()};
    };
    bool finite_vector(const cv::Vec3d &);
    using mark::observed::positive_definite;
}
