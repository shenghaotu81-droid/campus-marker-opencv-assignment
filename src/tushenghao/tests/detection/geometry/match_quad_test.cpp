#include "assertions.hpp"
#include "case_runner.hpp"
#include "block3_fixture.hpp"
#include "geometry_observation.hpp"
#include "decode_stage.hpp"
namespace
{
    using test_support::check;
    auto match(const fixture::Scene &s)
    {
        return mark::detector::match_quads(s.frame, mark::detector::extract_white_components(s.frame, {}), {});
    }

    void rotations()
    {
        for (double angle:{-8., -3., 0., 3., 8.})
            for (auto size:{cv::Size(960, 720), cv::Size(480, 360), cv::Size(960, 360)})
            {
                auto s = fixture::scene(size.width, size.height, angle);
                auto q = match(s);
                std::string order_reason;
                if (q.size() != 1)
                    std::cerr << "scene angle=" << angle << " work=" << size << '\n';
                check(q.size() == 1, "rotated/scaled MARK did not match uniquely");
                for (size_t i = 0; i < 4; ++i)
                    check(cv::norm((*mark::detector::order_screen_corners(q[0], order_reason))[i] - s.quad[i]) < 8, "screen corner changed");
                auto result = mark::detector::decode_stage(s.frame, q, fixture::corner_config());
                check(result.status_ == mark::detector::Status::DETECTED && result.detections_.size() == 1, "quad did not publish");
                for (size_t i = 0; i < 4; ++i)
                    check(cv::norm(cv::Point2d(result.detections_[0].corners_[result.detections_[0].orientation_[i]]) - s.quad[i]) < 2, "truth corner error");
            }
    }

    void changed_mount()
    {
        auto s = fixture::scene(960, 720, 90.);
        auto c = fixture::corner_config();
        c.physical_to_screen_ = {{1, 2, 3, 0}};
        auto q = mark::detector::match_quads(s.frame, mark::detector::extract_white_components(s.frame, {}), {});
        check(q.size() == 1, "changed mount not matched");
        auto d = mark::detector::decode_stage(s.frame, q, c);
        check(d.detections_.size() == 1, "configured mount not published");
        for (size_t i = 0; i < 4; ++i)
            check(cv::norm(cv::Point2d(d.detections_[0].corners_[d.detections_[0].orientation_[i]]) - s.quad[i]) < 2, "configured mount mapping ignored");
        check(d.detections_[0].orientation_ == c.physical_to_screen_, "publication inferred new identity");
    }

    void incomplete()
    {
        auto s = fixture::scene();
        s.frame.image_ = s.frame.image_.clone();
        cv::rectangle(s.frame.image_, {1048, 668, 35, 34}, cv::Scalar(0, 0, 0), cv::FILLED);
        check(match(s).empty(), "missing M generated predicted corner");
        cv::rectangle(s.frame.image_, {1052, 670, 29, 29}, cv::Scalar(255, 255, 255), cv::FILLED);
        check(match(s).empty(), "rectangle replaced M");
    }

    void clutter()
    {
        auto s = fixture::scene();
        s.frame.image_ = s.frame.image_.clone();
        cv::rectangle(s.frame.image_, {100, 100, 100, 100}, cv::Scalar(255, 255, 255), cv::FILLED);
        check(match(s).size() == 1, "independent bright rectangle corrupted match");
        s.frame.image_ = cv::Mat::zeros(s.frame.image_.size(), CV_8UC3);
        cv::rectangle(s.frame.image_, {100, 100, 100, 100}, cv::Scalar(255, 255, 255), cv::FILLED);
        check(match(s).empty(), "plain rectangle was MARK");
    }

    void wrong_l()
    {
        auto s = fixture::scene();
        s.frame.image_ = s.frame.image_.clone();
        cv::rectangle(s.frame.image_, {919, 669, 63, 63}, cv::Scalar(0, 0, 0), cv::FILLED);
        std::vector<cv::Point> wrong{{980, 670}, {980, 730}, {964, 730}, {964, 686}, {920, 686}, {920, 670}};
        cv::fillPoly(s.frame.image_, std::vector<std::vector<cv::Point> >{wrong}, cv::Scalar(255, 255, 255));
        check(match(s).empty(), "wrong-facing L matched as complete MARK");
    }

    void optional_s()
    {
        auto s = fixture::scene();
        s.frame.image_ = s.frame.image_.clone();
        cv::rectangle(s.frame.image_, {1018, 668, 21, 21}, cv::Scalar(0, 0, 0), cv::FILLED);
        check(match(s).size() == 1, "single missing S incorrectly became six-part completeness gate");
    }

    void two_targets()
    {
        auto s = fixture::scene();
        cv::Mat shifted;
        cv::Mat t = (cv::Mat_<double>(2, 3) << 1, 0, -600, 0, 1, -400);
        cv::warpAffine(s.frame.image_, shifted, t, s.frame.image_.size());
        cv::bitwise_or(s.frame.image_, shifted, s.frame.image_);
        auto q = match(s);
        check(q.size() == 2, "separated MARKs did not remain independent");
        check(mark::detector::decode_stage(s.frame, q, fixture::corner_config()).detections_.size() == 1, "work-only fake target acquired original evidence");
        s.frame.original_image_ = s.frame.original_image_.clone();
        cv::bitwise_or(s.frame.original_image_, shifted, s.frame.original_image_);
        check(mark::detector::decode_stage(s.frame, q, fixture::corner_config()).detections_.size() == 2, "two real targets did not publish independently");
    }

}
int main()
{
    return test_support::run({{"SmallShakeScaleIdentityAndTruth", rotations}, {"ChangedMount", changed_mount}, {"Incomplete", incomplete}, {"Clutter", clutter}, {"IndependentTargets", two_targets}, {"OptionalS", optional_s}, {"WrongFacingL", wrong_l}});
}
