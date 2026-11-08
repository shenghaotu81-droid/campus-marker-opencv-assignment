#include "block3_fixture.hpp"
#include "corners/corner_edge_fit.hpp"
#include "assertions.hpp"
#include <iostream>
#include <fstream>
int main()
{
    try{
        using test_support::check;
        for (double angle:{-8., 0., 8.})
        {
            auto s = fixture::scene(960, 720, angle);
            mark::detector::OriginalContourIndex index(s.frame);
            auto result = mark::detector::resolve_observed_corners(s.quad, index);
            check(bool(result.corners_), "single L2 failed");
            for (size_t i = 0; i < 4; ++i)
                check(cv::norm((*result.corners_)[i] - s.quad[i]) < 2., "truth corner precision dropped");
        }
        // 固定实拍轮廓验证圆角、断续支持下的单次 L2 拟合。
        std::ifstream input(std::filesystem::path(VISION_SOURCE_ROOT) / "tests/fixtures/frame784_corner.txt");
        size_t frame, match, physical, n;
        input >> frame >> match >> physical >> n;
        check(bool(input) && n > 0, "frozen fixture missing");
        cv::Point2d edges[4];
        for (auto &p:edges)
            input >> p.x >> p.y;
        std::vector<cv::Point> contour_(n);
        for (auto &p:contour_)
            input >> p.x >> p.y;
        std::string why;
        auto recovered = mark::detector::fit_observed_corner(contour_, edges[1], {edges[0], edges[3]}, why);
        check(recovered && cv::norm(*recovered - cv::Point2d(352.31130088909686, 322)) < .5, "old periodic real-world counterexample not recovered by single L2");

        check(!mark::detector::fit_observed_corner({}, {0, 0}, {{{1, 0}, {0, 1}}}, why), "empty support accepted");
        std::vector<cv::Point> line;
        for (int i = 0; i < 20; ++i)
            line.emplace_back(i, 0);
        check(!mark::detector::fit_observed_corner(line, {0, 0}, {{{1, 0}, {0, 1}}}, why), "one line invented corner");
        check(!mark::detector::fit_observed_corner(line, {0, 0}, {{{0, 0}, {0, 1}}}, why), "zero guidance accepted");
        return 0;
    }
    catch (const std::exception &e){
        std::cerr << e.what() << '\n';
        return 1;
    }
}
