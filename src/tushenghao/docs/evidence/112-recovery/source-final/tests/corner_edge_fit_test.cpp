// 边拟合独立反例：真实相切2px圆弧、真正近退化角/反向表示；不使用95°假门限。
#include "block3_fixture.hpp"
#include "corners/corner_edge_fit.hpp"
#include "corners/corner_evidence_validation.hpp"
#include "corners/periodic_line_fit.hpp"
#include "corners/corner_resolver.hpp"
#include "recovery112_reference.hpp"
#include "core/observed_geometry_utils.hpp"
#include "support_pixel_count.hpp"
#include <limits>
#include <iostream>
#include <stdexcept>
#include <fstream>

namespace
{
    void check(bool ok, const char *text)
    {
        if (!ok)
            throw std::runtime_error(text);
    }

    // 仅重算低残差会形成自证循环；用独立已知 a/b/c 检查轴对称、满秩和奇偶语义。
    void periodicParameters()
    {
        std::vector<cv::Point2d> support;
        for (int y = 20; y <= 60; ++y)
            support.emplace_back(100 + 0.125 * (y - 40) + 0.75 * (y % 2 ? 1 : -1), y);
        const auto vertical =
            mark::fitPeriodicLine(support, mark::LineFitModel::PeriodicLeastSquares);
        check(vertical && vertical->axis == 1, "periodic OLS axis wrong");
        check(std::abs(vertical->line[0] / vertical->line[1] - 0.125) < 1e-12 &&
                  std::abs(vertical->line[2] - 100) < 1e-12 &&
                  std::abs(vertical->coefficient - 0.75) < 1e-12,
              "known periodic parameters wrong");
        for (auto point : support)
            check(mark::periodicLineDistance(point, vertical->line, vertical->axis,
                                             vertical->coefficient) < 1e-12,
                  "known model residual wrong");
        for (auto &point : support)
            std::swap(point.x, point.y);
        const auto horizontal =
            mark::fitPeriodicLine(support, mark::LineFitModel::PeriodicPhaseMedian);
        check(horizontal && horizontal->axis == 0 &&
                  std::abs(horizontal->line[1] / horizontal->line[0] - 0.125) < 1e-12 &&
                  std::abs(horizontal->coefficient - 0.75) < 1e-12,
              "horizontal periodic symmetry wrong");
        support.clear();
        for (int y = 20; y <= 60; ++y)
            support.emplace_back(100 + (y % 2 ? 1 : -1), y);
        support.emplace_back(103, 20);
        const auto zero = mark::fitPeriodicLine(support, mark::LineFitModel::PeriodicZeroSlope);
        const auto l1 = mark::fitPeriodicLine(support, mark::LineFitModel::PeriodicL1);
        check(zero && zero->line[0] == 0 && zero->line[2] == 100 && zero->coefficient == 1,
              "zero slope did not use phase medians");
        check(l1 && std::abs(l1->line[2] - 100) < 1e-10 && std::abs(l1->coefficient - 1) < 1e-10,
              "L1 refinement wrong");
        check(!mark::fitPeriodicLine({{100, 20}, {100, 22}, {100, 24}},
                                     mark::LineFitModel::PeriodicLeastSquares),
              "single phase model accepted");
        check(!mark::fitPeriodicLine({{100, 20}, {101, 21}, {100, 20}, {101, 21}},
                                     mark::LineFitModel::PeriodicLeastSquares),
              "rank deficient model accepted");
        support[0].x = std::numeric_limits<double>::quiet_NaN();
        check(!mark::fitPeriodicLine(support, mark::LineFitModel::PeriodicZeroSlope),
              "nonfinite periodic support accepted");
        check(mark::scanPhase({-3, -2}, 0) == 1 && mark::scanPhase({-3, -2}, 1) == -1 &&
                  mark::scanPhase({0, 1.6}, 1) == -1,
              "integer or llround parity changed");
    }

    // 最终四角相同仍可能漏掉低分歧义候选；比较完整合格弧/配对序列及全部最终证据字段。
    void compareSearches(const std::vector<cv::Point> &contour,
                         const std::array<std::array<cv::Point2d, 2>, 2> &edges,
                         const mark::CornerConfig &config, mark::EdgePairFitStrategy strategy)
    {
        mark::EdgePairFitTrace indexed_trace, reference_trace;
        const auto indexed =
            mark::fitObservedEdgePair(contour, edges, config, strategy, &indexed_trace);
        const auto reference = mark::recovery112_reference::fitObservedEdgePair(
            contour, edges, config, strategy, &reference_trace);
        check(indexed_trace.arcs.size() == reference_trace.arcs.size(),
              "eligible arc coverage changed");
        for (size_t i = 0; i < indexed_trace.arcs.size(); ++i)
        {
            const auto &a = indexed_trace.arcs[i];
            const auto &b = reference_trace.arcs[i];
            check(std::tie(a.stage, a.begin, a.end, a.edge_mask, a.line, a.support, a.mean,
                           a.maximum, a.model_mean, a.model_maximum, a.model, a.axis,
                           a.coefficient) == std::tie(b.stage, b.begin, b.end, b.edge_mask, b.line,
                                                      b.support, b.mean, b.maximum, b.model_mean,
                                                      b.model_maximum, b.model, b.axis,
                                                      b.coefficient),
                  "eligible arc order/support/fit changed");
        }
        check(indexed_trace.pairs.size() == reference_trace.pairs.size(),
              "qualified pair coverage changed");
        for (size_t i = 0; i < indexed_trace.pairs.size(); ++i)
        {
            const auto &a = indexed_trace.pairs[i];
            const auto &b = reference_trace.pairs[i];
            check(std::tie(a.stage, a.endpoints, a.intersection, a.connector, a.corner_error,
                           a.first_extension, a.second_extension) ==
                      std::tie(b.stage, b.endpoints, b.intersection, b.connector, b.corner_error,
                               b.first_extension, b.second_extension),
                  "pair order/geometry/connector changed");
        }
        check(bool(indexed.evidence) == bool(reference.evidence), "reference success changed");
        if (indexed.evidence)
        {
            const auto &a = *indexed.evidence;
            const auto &b = *reference.evidence;
            check(std::tie(a.intersection_, a.original_support_arcs_, a.original_turn_arc_,
                           a.observed_segment_ids_, a.observed_segment_end_ids_, a.line_a_,
                           a.line_b_, a.line_mean_residual_px_, a.line_max_residual_px_,
                           a.support_extension_px_, a.corner_error_px_, a.error_,
                           a.line_model_kinds_, a.periodic_axes_, a.periodic_coefficients_,
                           a.model_mean_residual_px_, a.model_max_residual_px_) ==
                      std::tie(b.intersection_, b.original_support_arcs_, b.original_turn_arc_,
                               b.observed_segment_ids_, b.observed_segment_end_ids_, b.line_a_,
                               b.line_b_, b.line_mean_residual_px_, b.line_max_residual_px_,
                               b.support_extension_px_, b.corner_error_px_, b.error_,
                               b.line_model_kinds_, b.periodic_axes_, b.periodic_coefficients_,
                               b.model_mean_residual_px_, b.model_max_residual_px_),
                  "reference evidence/tie-break changed");
        }
        else
            check(indexed.reason.find("AMBIGUOUS_EDGE_PAIR") ==
                      reference.reason.find("AMBIGUOUS_EDGE_PAIR"),
                  "reference ambiguity changed");
    }

    void indexedSearchReference()
    {
        const std::array<std::array<cv::Point2d, 2>, 2> edges{
            {{cv::Point2d(30, 62), cv::Point2d(30, 30)},
             {cv::Point2d(30, 30), cv::Point2d(62, 30)}}};
        auto config = fixture::cornerConfig();
        config.min_line_points_ = 10;
        config.max_line_fit_error_ = 0.5;
        config.observation_budget_ = mark::CornerObservationBudget{3, 9, 4.5, 2, 13.5, 9.5, 14};
        size_t comparisons = 0;
        for (int variation = 0; variation < 5; ++variation)
        {
            cv::Mat image = cv::Mat::zeros(96, 96, CV_8UC1);
            std::vector<cv::Point> polygon{{30, 30}, {62, 30}, {62, 62}, {30, 62}};
            if (variation == 1)
                polygon = {{32, 30}, {62, 30}, {62, 62}, {30, 62}, {30, 32}};
            if (variation == 2)
                polygon = {{30, 30}, {45, 30}, {45, 26}, {45, 30}, {62, 30}, {62, 62}, {30, 62}};
            cv::fillPoly(image, std::vector<std::vector<cv::Point>>{polygon}, cv::Scalar(255));
            if (variation >= 3)
            {
                const cv::Mat original = image.clone();
                image.setTo(0);
                for (int y = 0; y < image.rows; ++y)
                    for (int x = 0; x < image.cols; ++x)
                    {
                        const int shift = variation == 3 ? (y % 2 ? 1 : -1) : (y % 4 == 0 ? 1 : 0);
                        if (x + shift >= 0 && x + shift < image.cols)
                            image.at<unsigned char>(y, x + shift) =
                                original.at<unsigned char>(y, x);
                    }
            }
            std::vector<std::vector<cv::Point>> contours;
            cv::findContours(image, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
            for (auto strategy : {mark::EdgePairFitStrategy::OriginalOnly,
                                  mark::EdgePairFitStrategy::LinearRecovery,
                                  mark::EdgePairFitStrategy::FastRecovery})
            {
                compareSearches(contours.at(0), edges, config, strategy);
                auto reversed = contours.at(0);
                std::reverse(reversed.begin(), reversed.end());
                compareSearches(reversed, edges, config, strategy);
                comparisons += 2;
            }
        }
        // 阈值前后一 ULP 仍需走原局部连接求和；小轮廓完整全端点补查保持覆盖。
        cv::Mat image = cv::Mat::zeros(96, 96, CV_8UC1);
        cv::rectangle(image, {30, 30, 33, 33}, cv::Scalar(255), cv::FILLED);
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(image, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
        for (double trim : {0.0, 2.0, std::sqrt(2.0)})
            for (double connection : {std::nextafter(4.0, 0.0), 4.0, std::nextafter(4.0, 5.0)})
            {
                config.observation_budget_->turn_trim_distance_px = trim;
                config.observation_budget_->max_turn_connection_length_px = connection;
                compareSearches(contours.at(0), edges, config,
                                mark::EdgePairFitStrategy::LinearRecovery);
                ++comparisons;
            }
        std::cout << "reference comparisons=" << comparisons
                  << " (all arcs/pairs/evidence exact)\n";
    }

    // 模型增加自由度后必须证明证据约束：真实奇偶轮廓通过，c/轴/模型/两类误差篡改拒绝。
    void periodicEvidenceContract()
    {
        // 轴向矩形的链码可能刚好满足旧 0.5；旋转图纸后再扰动，确实需要周期证据。
        auto scene = fixture::scene(1440, 1080, 2.0);
        const cv::Mat source = scene.frame.original_image_.clone();
        scene.frame.original_image_.setTo(cv::Scalar(0, 0, 0));
        for (int y = 0; y < source.rows; ++y)
            for (int x = 0; x < source.cols; ++x)
            {
                const int target = x + (y % 2 ? 1 : -1);
                if (target >= 0 && target < source.cols)
                    scene.frame.original_image_.at<cv::Vec3b>(y, target) =
                        source.at<cv::Vec3b>(y, x);
            }
        auto config = fixture::cornerConfig();
        config.min_line_points_ = 10;
        config.max_line_fit_error_ = 0.5;
        config.observation_budget_ = mark::CornerObservationBudget{3, 9, 4.5, 2, 13.5, 9.5, 14};
        const auto resolved =
            mark::resolveObservedCorners(scene.frame, scene.hypothesis, scene.geometry, config);
        check(bool(resolved.measurement_), resolved.rejection_reason_.c_str());
        std::optional<mark::CornerEvidence> selected;
        size_t periodic_edge = 0;
        int corner_index = -1;
        for (int corner = 0; corner < 4; ++corner)
            for (size_t edge = 0; edge < 2; ++edge)
            {
                const auto &candidate = resolved.measurement_->evidence_[corner];
                if (!selected && candidate.line_model_kinds_[edge] != mark::LineFitModel::Legacy &&
                    candidate.line_mean_residual_px_[edge] > 0.5)
                {
                    selected = candidate;
                    periodic_edge = edge;
                    corner_index = corner;
                }
            }
        check(bool(selected), "fixture did not require periodic model");
        auto evidence = *selected;
        std::string reason;
        check(mark::validateCornerEvidence(evidence, evidence.intersection_, corner_index, config,
                                           reason),
              reason.c_str());
        check(periodic_edge < 2 && evidence.line_mean_residual_px_[periodic_edge] > 0.5 &&
                  evidence.model_mean_residual_px_[periodic_edge] <= 0.5,
              "raw residual was replaced by corrected error");
        for (int alteration = 0; alteration < 6; ++alteration)
        {
            auto changed = evidence;
            switch (alteration)
            {
            case 0:
                changed.periodic_coefficients_[periodic_edge] += 0.25;
                break;
            case 1:
                changed.periodic_axes_[periodic_edge] = 1 - changed.periodic_axes_[periodic_edge];
                break;
            case 2:
                changed.line_model_kinds_[periodic_edge] = static_cast<mark::LineFitModel>(99);
                break;
            case 3:
                changed.model_mean_residual_px_[periodic_edge] += 0.01;
                break;
            case 4:
                changed.line_mean_residual_px_[periodic_edge] += 0.01;
                break;
            case 5:
                changed.line_model_kinds_[periodic_edge] = mark::LineFitModel::Legacy;
                break;
            }
            check(!mark::validateCornerEvidence(changed, changed.intersection_, corner_index,
                                                config, reason),
                  "periodic evidence tampering accepted");
        }
    }

    // 主动检查在 Release 也执行；只测工具计数，不修改生产弧拟合输入。
    void uniqueSupportPixelCount()
    {
        using block3_fixture_tools::count_unique_support_pixels;
        check(count_unique_support_pixels({}) == 0, "empty support count");
        check(count_unique_support_pixels({{1, 1}, {2, 1}, {3, 1}}) == 3, "distinct support count");
        const std::vector<cv::Point2d> repeated(5, {7, 8});
        const auto before = repeated;
        check(count_unique_support_pixels(repeated) == 1 && repeated == before,
              "repeated support mutated or overcounted");
        const std::vector<cv::Point2d> retraced{{1, 1}, {2, 1}, {3, 1}, {2, 1}, {1, 1}, {3, 1}};
        check(count_unique_support_pixels(retraced) == 3 && retraced.size() == 6,
              "nonadjacent retrace overcounted");
        const std::vector<cv::Point2d> first{{1, 1}, {1, 1}, {2, 1}};
        const std::vector<cv::Point2d> second{{1, 1}, {3, 1}, {3, 1}};
        check(count_unique_support_pixels(first) == 2 && count_unique_support_pixels(second) == 2,
              "separate arcs must be counted independently");
        check(count_unique_support_pixels({{1, 2}, {std::nextafter(1.0, 2.0), 2}}) == 2,
              "distinct finite pixels merged by tolerance");
        for (const auto bad : {cv::Point2d(std::numeric_limits<double>::quiet_NaN(), 0),
                               cv::Point2d(0, std::numeric_limits<double>::quiet_NaN()),
                               cv::Point2d(std::numeric_limits<double>::infinity(), 0),
                               cv::Point2d(0, -std::numeric_limits<double>::infinity())})
        {
            bool rejected = false;
            try
            {
                count_unique_support_pixels({{1, 1}, bad});
            }
            catch (const std::invalid_argument &)
            {
                rejected = true;
            }
            check(rejected, "nonfinite support silently accepted");
        }
    }

    // 将连续圆弧栅格化为真实图像，轮廓提取仍保留所有像素，不使用blur/形态学圆角。
    void roundedCorner()
    {
        std::vector<cv::Point> polygon;
        for (int step = 0; step <= 32; ++step)
        {
            double a = CV_PI + CV_PI / 2 * step / 32;
            polygon.emplace_back(cv::Point2d(102 + 2 * std::cos(a), 102 + 2 * std::sin(a)));
        }
        polygon.insert(polygon.end(), {{130, 100}, {130, 130}, {100, 130}});
        cv::Mat image = cv::Mat::zeros(200, 200, CV_8UC1);
        cv::fillPoly(image, std::vector<std::vector<cv::Point>>{polygon}, cv::Scalar(255));
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(image, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
        std::array<std::array<cv::Point2d, 2>, 2> edges{
            {{cv::Point2d(100, 130), cv::Point2d(100, 100)},
             {cv::Point2d(100, 100), cv::Point2d(130, 100)}}};
        auto result = mark::fitObservedEdgePair(contours.at(0), edges, fixture::cornerConfig());
        check(bool(result.evidence), result.reason.c_str());
        check(cv::norm(result.evidence->intersection_ - cv::Point2d(100, 100)) < 1,
              "rounded nominal intersection wrong");
        check(result.evidence->original_turn_arc_.size() > 2, "rounded connector not measured");
    }

    // 同一无向夹角对0°和180°表示一致；真实小角不能误认为安全大夹角。
    void parallelEdges()
    {
        cv::Point2d a{1, 0}, b{1, 0.001};
        check(mark::observed::angleDeg(a, b) < 1 && mark::observed::angleDeg(a, -b) < 1,
              "anti-parallel not folded to acute angle");
        cv::Mat image = cv::Mat::zeros(200, 300, CV_8UC1);
        std::vector<cv::Point> polygon{{20, 20}, {120, 20}, {220, 21}, {220, 70}, {20, 70}};
        cv::fillPoly(image, std::vector<std::vector<cv::Point>>{polygon}, cv::Scalar(255));
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(image, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
        std::array<std::array<cv::Point2d, 2>, 2> edges{
            {{cv::Point2d(20, 20), cv::Point2d(120, 20)},
             {cv::Point2d(120, 20), cv::Point2d(220, 21)}}};
        auto config = fixture::cornerConfig();
        config.approximation_epsilon_ = 0.1;
        auto result = mark::fitObservedEdgePair(contours.at(0), edges, config);
        check(!result.evidence && !result.reason.empty(),
              "nearly parallel pair produced valid corner");
    }

    // 两条长直边拟合完美，但被大倒角隔开，交点远离真实转折，不能补名义角。
    void remoteIntersection()
    {
        cv::Mat image = cv::Mat::zeros(300, 300, CV_8UC1);
        cv::fillPoly(image,
                     std::vector<std::vector<cv::Point>>{
                         {{150, 100}, {200, 100}, {200, 200}, {100, 200}, {100, 150}}},
                     cv::Scalar(255));
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(image, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
        std::array<std::array<cv::Point2d, 2>, 2> edges{
            {{cv::Point2d(100, 200), cv::Point2d(100, 100)},
             {cv::Point2d(100, 100), cv::Point2d(200, 100)}}};
        auto config = fixture::cornerConfig();
        config.observation_budget_->max_support_extension_px = 100;
        config.observation_budget_->max_turn_connection_length_px = 100;
        auto result = mark::fitObservedEdgePair(contours[0], edges, config);
        check(!result.evidence, "remote infinite-line intersection published");
    }

    // 已保留H-v1失败数据；旧单简化段实现会断开直边末端，预算完全保持冻结v1。
    void splitRoundedBoundary()
    {
        std::ifstream source(std::filesystem::path(__FILE__).parent_path() /
                             "data/rounded_split_boundary.txt");
        check(bool(source), "rounded regression fixture missing");
        cv::Point2d expected;
        source >> expected.x >> expected.y;
        std::array<std::array<cv::Point2d, 2>, 2> edges{};
        for (auto &edge : edges)
            for (auto &point : edge)
                source >> point.x >> point.y;
        std::vector<cv::Point> pixels;
        double x, y;
        while (source >> x >> y)
            pixels.emplace_back(cv::Point2d(x, y));
        cv::Mat image = cv::Mat::zeros(1080, 1440, CV_8UC1);
        cv::fillPoly(image, std::vector<std::vector<cv::Point>>{pixels}, cv::Scalar(255));
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(image, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
        auto c = fixture::cornerConfig();
        c.min_line_points_ = 10;
        c.max_line_fit_error_ = 0.5;
        c.max_corner_error_ = 1.5;
        c.observation_budget_ = mark::CornerObservationBudget{1, 2, 4.5, 2, 13.5, 9.5, 14};
        auto result = mark::fitObservedEdgePair(contours.at(0), edges, c);
        check(bool(result.evidence), result.reason.c_str());
        check(cv::norm(result.evidence->intersection_ - expected) <= 2,
              "split boundary truth error exceeded");
    }

    // 指定外边被大量小简化段拆分；完整连续支持必须覆盖两段以上，不能拼非连续点。
    void manySimplifiedSegments()
    {
        std::vector<cv::Point> polygon;
        for (int x = 100; x <= 160; ++x)
            polygon.emplace_back(x, 100 + (x % 4 == 2));
        polygon.insert(polygon.end(), {{160, 160}, {100, 160}});
        cv::Mat image = cv::Mat::zeros(240, 240, CV_8UC1);
        cv::fillPoly(image, std::vector<std::vector<cv::Point>>{polygon}, cv::Scalar(255));
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(image, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
        auto c = fixture::cornerConfig();
        c.approximation_epsilon_ = 0.1;
        c.min_line_points_ = 10;
        c.observation_budget_->min_support_span_px = 14;
        std::vector<cv::Point> simplified;
        cv::approxPolyDP(contours[0], simplified, c.approximation_epsilon_, true);
        check(simplified.size() > 30, "fragmentation fixture lost simplified vertices");
        std::array<std::array<cv::Point2d, 2>, 2> edges{
            {{cv::Point2d(100, 160), cv::Point2d(100, 100)},
             {cv::Point2d(100, 100), cv::Point2d(160, 100)}}};
        auto result = mark::fitObservedEdgePair(contours[0], edges, c);
        check(bool(result.evidence), result.reason.c_str());
        check(cv::norm(result.evidence->intersection_ - cv::Point2d(100, 100)) < 1,
              "fragmented edge intersection wrong");
    }

    // 视频第1帧真实阈值轮廓含51个简化顶点，外边不能受1～2段候选上限阻断。
    void fragmentedVideoBoundary()
    {
        std::ifstream source(std::filesystem::path(__FILE__).parent_path() /
                             "data/video_fragmented_boundary.txt");
        check(bool(source), "video regression fixture missing");
        std::array<std::array<cv::Point2d, 2>, 2> edges{};
        for (auto &edge : edges)
            for (auto &point : edge)
                source >> point.x >> point.y;
        std::vector<cv::Point> contour;
        int x, y;
        while (source >> x >> y)
            contour.emplace_back(x, y);
        auto c = fixture::cornerConfig();
        c.min_line_points_ = 10;
        c.max_line_fit_error_ = 0.5;
        c.max_corner_error_ = 1.5;
        c.observation_budget_ = mark::CornerObservationBudget{3, 9, 4.5, 2, 13.5, 9.5, 14};
        auto result = mark::fitObservedEdgePair(contour, edges, c);
        check(bool(result.evidence), result.reason.c_str());
        check(result.evidence->corner_error_px_ <= 1.5, "video corner gate changed");
        // 真实弧证据送进生产复算校验，防止只通过局部fit却违反连续性或端点绑定。
        auto evidence = *result.evidence;
        evidence.stable_id_ = "frame-1/M1";
        evidence.component_id_ = 7;
        evidence.physical_corner_ = static_cast<mark::PhysicalCorner>(1);
        evidence.model_vertex_id_ = 1;
        evidence.model_edge_ids_ = {0, 1};
        std::string reason;
        check(mark::validateCornerEvidence(evidence, evidence.intersection_, 1, c, reason),
              reason.c_str());
        for (auto &arc : result.evidence->original_support_arcs_)
            for (auto p : arc)
                check(std::find(contour.begin(), contour.end(), cv::Point(p)) != contour.end(),
                      "fabricated video support");
    }

}

int main()
{
    int failures = 0;
    for (auto item :
         {std::pair<const char *, void (*)()>{"UniqueSupportPixelCount", uniqueSupportPixelCount},
          {"RoundedCornerDoesNotCrash", roundedCorner},
          {"ParallelEdgesFail", parallelEdges},
          {"RemoteIntersectionFails", remoteIntersection},
          {"SplitRoundedBoundary", splitRoundedBoundary},
          {"FragmentedVideoBoundary", fragmentedVideoBoundary},
          {"ManySimplifiedSegments", manySimplifiedSegments},
          {"PeriodicParameters", periodicParameters},
          {"PeriodicEvidenceContract", periodicEvidenceContract},
          {"IndexedSearchReference", indexedSearchReference}})
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
