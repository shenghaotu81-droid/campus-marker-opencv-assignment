#include "corners/publication/detection_publication.hpp"
namespace mark
{
    std::optional<Detection> publishFloatDetection(const CornerPoints &points, cv::Size size, const CornerConfig &config, std::string &reason)
    {
        CornerPoints rounded;
        for (size_t i = 0; i < 4; ++i)
            rounded[i] = cv::Point2f(points[i]);
        auto ordered = orderScreenCorners(rounded, reason);
        if (!ordered)
            return {};
        Detection d;
        d.orientation = config.physical_to_screen_;
        for (size_t i = 0; i < 4; ++i)
        {
            auto p = (*ordered)[i];
            if (p.x < 0 || p.y < 0 || p.x >= size.width || p.y >= size.height)
            {
                reason = "CORNER_OUT_OF_IMAGE";
                return {};
            }
            d.corners[i] = cv::Point2f(p);
        }
        return d;
    }

}
