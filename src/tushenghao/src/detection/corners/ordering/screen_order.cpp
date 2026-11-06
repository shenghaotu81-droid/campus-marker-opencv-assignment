#include "vision/types/detection/corner_types.hpp"
#include <algorithm>
#include <cmath>
namespace mark
{
    std::optional<CornerPoints> orderScreenCorners(const CornerPoints &p, std::string &reason)
    {
        reason.clear();
        double sign = 0;
        for (size_t i = 0; i < 4; ++i)
        {
            if (!std::isfinite(p[i].x) || !std::isfinite(p[i].y))
            {
                reason = "NON_FINITE_CORNER";
                return {};
            }
            double cross = (p[(i + 1) % 4] - p[i]).cross(p[(i + 2) % 4] - p[(i + 1) % 4]);
            if (!std::isfinite(cross) || cross == 0 || (i && (cross > 0) != (sign > 0)))
            {
                reason = "DEGENERATE_QUAD";
                return {};
            }
            sign = cross;
        }
        auto q = p;
        std::sort(q.begin(), q.end(), [](auto a, auto b)
        {
            return a.y < b.y || (a.y == b.y && a.x < b.x);
        });
        if (q[0].x > q[1].x)
            std::swap(q[0], q[1]);
        if (q[2].x < q[3].x)
            std::swap(q[2], q[3]);
        return q;
    }

}
