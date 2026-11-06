// 只读冻结旧指导输入；不向生产拟合器添加trace。
#include "corners/fitting/corner_edge_fit.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
int main(int argc, char **argv)
{
    if (argc != 3) return 1;
    std::ifstream in(argv[1]);
    std::ofstream out(argv[2]);
    if (!in || !out) return 1;
    out << std::setprecision(17);
    size_t frame, index, physical, n, calls = 0, failed = 0;
    while (in >> frame >> index >> physical >> n)
    {
        cv::Point2d edges[4];
        for (auto &p : edges) in >> p.x >> p.y;
        std::vector<cv::Point> contour(n);
        for (auto &p : contour) in >> p.x >> p.y;
        if (!in) return 1;
        std::string why;
        auto fit = mark::fitObservedCorner(contour, edges[1], {edges[0], edges[3]}, why);
        ++calls;
        out << frame << ' ' << index << ' ' << physical;
        if (fit) out << ' ' << fit->x << ' ' << fit->y;
        else { ++failed; out << " FAIL " << why; }
        out << '\n';
    }
    std::cout << "calls=" << calls << " failed=" << failed << '\n';
    return calls ? 0 : 1;
}
