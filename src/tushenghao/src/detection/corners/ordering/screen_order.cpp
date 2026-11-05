#include "vision/types/detection/corner_types.hpp"
#include "vision/config/detector_config.hpp"
#include <algorithm>
namespace mark {
ScreenOrderResult orderScreenCorners(const std::array<cv::Point2d,4>& points,
                                     const CornerConfig& config) {
    double area=0,sign=0;
    for (size_t i=0;i<4;++i) {
        if (!std::isfinite(points[i].x) || !std::isfinite(points[i].y))
            return screenFailure("NON_FINITE_CORNER");
        for (size_t j=0;j<i;++j)
            if (cv::norm(points[i]-points[j])<1e-9) return screenFailure("DUPLICATE_CORNER");
        const double cross=(points[(i+1)%4]-points[i]).cross(points[(i+2)%4]-points[(i+1)%4]);
        if (cross==0 || (i && (cross>0)!=(sign>0))) return screenFailure("NON_CONVEX_QUAD");
        sign=cross;area+=points[i].cross(points[(i+1)%4]);
    }
    if (std::abs(area)<2e-9) return screenFailure("DEGENERATE_QUAD");
    auto mapping=config.physical_to_screen_;
    std::sort(mapping.begin(),mapping.end());
    if (mapping!=std::array<int,4>{{0,1,2,3}}) return screenFailure("INVALID_FIXED_MAPPING");
    // 先按y分上下两点，再各按x分左右。上下带不交叠的小晃动不会交换槽位。
    auto sorted=points;
    std::sort(sorted.begin(),sorted.end(),[](auto a,auto b){return a.y<b.y || (a.y==b.y && a.x<b.x);});
    if (sorted[0].x>sorted[1].x) std::swap(sorted[0],sorted[1]);
    if (sorted[2].x<sorted[3].x) std::swap(sorted[2],sorted[3]);
    ScreenOrder order;
    order.screen_points_=sorted;
    order.physical_to_screen_=config.physical_to_screen_;
    return screenSuccess(order);
}
}
