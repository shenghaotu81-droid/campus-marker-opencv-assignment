// 输入真实BGR fixture，隔离assignment后调用冻结签名；输出断言通过/失败。
// 原“全黑图+手填轮廓”的成功用例改为反证，新增真正画面正例，保留旧负例目的。
#include "block3_fixture.hpp"
#include "corners/corner_resolver.hpp"
#include "corners/corner_observation.hpp"
#include "core/config_error.hpp"
#include <iostream>
#include <stdexcept>

namespace
{
    void check(bool ok, const char *message)
    {
        if (!ok)
            throw std::runtime_error(message);
    }

    mark::CornerResolution resolve(const fixture::Scene &s,
                                   mark::CornerConfig c = fixture::cornerConfig())
    {
        return mark::resolveObservedCorners(s.frame, s.hypothesis, s.geometry, c);
    }

    void failure(const mark::CornerResolution &r)
    {
        check(r.status_ == mark::CornerResolutionStatus::FAILED && !r.measurement_ &&
                  !r.rejection_reason_.empty(),
              "failed result contract violated");
    }

    // 检查物理位置、来源和八条真实支持弧，不再仅检查finite。
    void direct()
    {
        auto s = fixture::scene();
        auto before = s.frame.original_image_.clone();
        auto r = resolve(s);
        check(r.measurement_.has_value(), r.rejection_reason_.c_str());
        const std::array<cv::Point2d, 4> expected{
            {{920, 670}, {1080, 670}, {1080, 830}, {920, 830}}};
        for (int i = 0; i < 4; ++i)
        {
            const auto &e = r.measurement_->evidence_[i];
            check(cv::norm(r.measurement_->physical_corners_[i] - expected[i]) < 1,
                  "wrong physical corner");
            check(e.original_observation_ && e.stable_id_.size() > 0 &&
                      e.original_support_arcs_[0].size() >= 3 &&
                      e.original_support_arcs_[1].size() >= 3 && e.frame_id_ == 7,
                  "missing current evidence");
        }
        check(cv::norm(before, s.frame.original_image_, cv::NORM_INF) == 0,
              "input pixels modified");
    }

    // 原图右下角合法，多工作尺度和各向异性resize不改变原图四点。
    void scale()
    {
        auto baseline = resolve(fixture::scene());
        check(bool(baseline.measurement_), "baseline failed");
        for (auto size : {cv::Size(480, 360), cv::Size(960, 720), cv::Size(960, 360)})
        {
            auto r = resolve(fixture::scene(size.width, size.height));
            check(bool(r.measurement_), r.rejection_reason_.c_str());
            for (int i = 0; i < 4; ++i)
                check(cv::norm(r.measurement_->physical_corners_[i] -
                               baseline.measurement_->physical_corners_[i]) < 1e-8,
                      "working scale affected original evidence");
        }
    }

    void blackImage()
    {
        auto s = fixture::scene();
        s.frame.original_image_ = cv::Mat::zeros(s.frame.original_image_.size(), CV_8UC3);
        failure(resolve(s));
    }

    // 原分割会重复执行；同 decode 返回同一缓存，下一次 wrapper 即便 ID 不变也重读图像。
    void originalIndexLifetime()
    {
        auto scene = fixture::scene();
        const auto config = fixture::cornerConfig();
        mark::OriginalContourIndex index(scene.frame, scene.frame.frame_id_,
                                         *config.observation_budget_);
        const auto &component = scene.frame.components_.front();
        const auto &first = index.observe(component);
        check(first.reason.empty() && !first.contour.empty(), "index failed original mapping");
        check(&first == &index.observe(component), "component was not cached");
        auto next = scene.frame;
        next.original_image_ = cv::Mat::zeros(next.original_image_.size(), CV_8UC3);
        check(!mark::observeOriginalContour(next, component, *config.observation_budget_)
                   .reason.empty(),
              "same ID reused old original contour");
        next.frame_id_ += 1;
        const auto wrong =
            mark::resolveObservedCorners(next, scene.hypothesis, scene.geometry, config, index);
        check(wrong.rejection_reason_ == "ORIGINAL_INDEX_FRAME_MISMATCH",
              "frame mismatch accepted");
    }

    void missingM()
    {
        auto s = fixture::scene();
        s.hypothesis.assignments_.erase(s.hypothesis.assignments_.begin() + 3);
        auto r = resolve(s);
        failure(r);
        check(r.rejection_reason_ == "MISSING_M_ASSIGNMENT", "M fallback remains");
    }

    void missingPixels()
    {
        auto s = fixture::scene();
        s.frame.original_image_ = s.frame.original_image_.clone();
        cv::rectangle(s.frame.original_image_, {1048, 668, 35, 34}, cv::Scalar(0, 0, 0),
                      cv::FILLED);
        failure(resolve(s));
    }

    // 保留其余片，切除M整条上外边及其凸角；旧工作轮廓不能替当前原图补边。
    void severedEdge()
    {
        auto s = fixture::scene();
        s.frame.original_image_ = s.frame.original_image_.clone();
        cv::rectangle(s.frame.original_image_, {1052, 668, 31, 10}, cv::Scalar(0, 0, 0),
                      cv::FILLED);
        failure(resolve(s));
    }

    // 额外白线把必要片粘到其他白块时，完整轮廓双向关联必须拒绝。
    void gluedComponent()
    {
        auto s = fixture::scene();
        s.frame.original_image_ = s.frame.original_image_.clone();
        cv::line(s.frame.original_image_, {920, 670}, {1080, 670}, cv::Scalar(255, 255, 255), 3);
        failure(resolve(s));
    }

    // 不允许普通矩形替代M，纵使它在同一预测位置且有漂亮凸角。
    void rectangularM()
    {
        auto s = fixture::scene();
        s.frame.original_image_ = s.frame.original_image_.clone();
        cv::rectangle(s.frame.original_image_, {1048, 668, 35, 34}, cv::Scalar(0, 0, 0),
                      cv::FILLED);
        cv::rectangle(s.frame.original_image_, {1052, 670, 29, 29}, cv::Scalar(255, 255, 255),
                      cv::FILLED);
        failure(resolve(s));
    }

    // 当前原图逐次重取证；下一帧全黑不能复用上一帧成功结果。
    void consecutiveFrames()
    {
        auto s = fixture::scene();
        check(bool(resolve(s).measurement_), "first frame failed");
        ++s.frame.frame_id_;
        s.frame.original_image_ = cv::Mat::zeros(s.frame.original_image_.size(), CV_8UC3);
        failure(resolve(s));
    }

    // 将真实目标平移到x=0，组件与图像仍一致，但原图外边界证据必须拒绝。
    void originalBorder()
    {
        auto s = fixture::scene();
        cv::Mat moved, shift = (cv::Mat_<double>(2, 3) << 1, 0, -920, 0, 1, 0);
        cv::warpAffine(s.frame.original_image_, moved, shift, s.frame.original_image_.size());
        s.frame.original_image_ = moved;
        s.frame.image_ = moved.clone();
        s.hypothesis.affine_transform_.at<double>(0, 2) -= 920;
        for (auto &c : s.frame.components_)
        {
            for (auto &p : c.contour_)
                p.x -= 920;
            c.bounding_box_.x -= 920;
        }
        failure(resolve(s));
    }

    void wrongM()
    {
        auto s = fixture::scene();
        s.hypothesis.assignments_[3].component_id_ = s.hypothesis.assignments_[4].component_id_;
        s.hypothesis.assignments_.erase(s.hypothesis.assignments_.begin() + 4);
        failure(resolve(s));
    }

    void shortSupport()
    {
        auto s = fixture::scene();
        auto c = fixture::cornerConfig();
        c.min_line_points_ = 1000;
        failure(resolve(s, c));
    }

    void truncated()
    {
        auto s = fixture::scene();
        s.hypothesis.completeness_ = mark::GeometryCompleteness::CLEARLY_INCOMPLETE;
        failure(resolve(s));
    }

    void invalidAffine()
    {
        auto s = fixture::scene();
        s.hypothesis.affine_transform_.at<double>(0, 0) = -2;
        failure(resolve(s));
        s.hypothesis.affine_transform_ = cv::Mat::ones(1, 1, CV_32F);
        failure(resolve(s));
    }

    void missingBudget()
    {
        auto s = fixture::scene();
        auto c = fixture::cornerConfig();
        c.observation_budget_.reset();
        bool threw = false;
        try
        {
            resolve(s, c);
        }
        catch (const mark::ConfigError &)
        {
            threw = true;
        }
        check(threw, "missing budget swallowed as empty");
    }
}

int main()
{
    const std::pair<const char *, void (*)()> cases[] = {
        {"DirectCornerReturnsSuccess", direct},
        {"OriginalScaleAndReadOnly", scale},
        {"BlackOriginalRejectsFakeContour", blackImage},
        {"OriginalIndexLifetime", originalIndexLifetime},
        {"MissingMAssignment", missingM},
        {"MissingEdgeFails", missingPixels},
        {"SeveredOriginalEdge", severedEdge},
        {"GluedOriginalComponent", gluedComponent},
        {"RectangleCannotReplaceM", rectangularM},
        {"ConsecutiveFramesUseCurrentEvidence", consecutiveFrames},
        {"RealOriginalBorderRejects", originalBorder},
        {"WrongMIdentity", wrongM},
        {"ShortEdgeFails", shortSupport},
        {"TruncatedCornerFails", truncated},
        {"InvalidAffineFails", invalidAffine},
        {"MissingBudgetIsConfigError", missingBudget}};
    int failures = 0;
    for (auto item : cases)
    {
        try
        {
            item.second();
            std::cout << "PASS " << item.first << '\n';
        }
        catch (const std::exception &e)
        {
            ++failures;
            std::cerr << "FAIL " << item.first << ": " << e.what() << '\n';
        }
    }
    return failures ? 1 : 0;
}
