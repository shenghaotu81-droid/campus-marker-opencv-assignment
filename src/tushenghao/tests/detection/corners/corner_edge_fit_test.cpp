#include "block3_fixture.hpp"
#include "corners/fitting/corner_edge_fit.hpp"
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
            mark::OriginalContourIndex index(s.frame);
            auto result = mark::resolveObservedCorners(s.quad, index);
            check(bool(result.corners), "single L2 failed");
            for (size_t i = 0; i < 4; ++i)
                check(cv::norm((*result.corners)[i] - s.quad[i]) < 2., "truth corner precision dropped");
        }
        // 旧实拍中曾依赖周期回退的角：现在同一轮廓/指导只需一次L2。
        std::ifstream input(std::filesystem::path(VISION_SOURCE_ROOT) / "tests/fixtures/frame784_corner.txt");
        size_t frame, match, physical, n;
        input >> frame >> match >> physical >> n;
        check(bool(input) && n > 0, "frozen fixture missing");
        cv::Point2d edges[4];
        for (auto &p:edges)
            input >> p.x >> p.y;
        std::vector<cv::Point> contour(n);
        for (auto &p:contour)
            input >> p.x >> p.y;
        std::string why;
        auto recovered = mark::fitObservedCorner(contour, edges[1], {edges[0], edges[3]}, why);
        check(recovered && cv::norm(*recovered - cv::Point2d(352.31130088909686, 322)) < .5, "old periodic real-world counterexample not recovered by single L2");

        check(!mark::fitObservedCorner({}, {0, 0}, {{{1, 0}, {0, 1}}}, why), "empty support accepted");
        std::vector<cv::Point> line;
        for (int i = 0; i < 20; ++i)
            line.emplace_back(i, 0);
        check(!mark::fitObservedCorner(line, {0, 0}, {{{1, 0}, {0, 1}}}, why), "one line invented corner");
        check(!mark::fitObservedCorner(line, {0, 0}, {{{0, 0}, {0, 1}}}, why), "zero guidance accepted");
        return 0;
    }
    catch (const std::exception &e){
        std::cerr << e.what() << '\n';
        return 1;
    }
}
