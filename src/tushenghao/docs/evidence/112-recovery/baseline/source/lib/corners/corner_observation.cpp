// 输入assignment已确定的工作图组件，仅从当前原图量测；多来源不能排除时拒绝。
// 原实现裁ROI再closed=true会造边；这里从原图完整轮廓取证，无ROI人工闭合边。
#include "corners/corner_observation.hpp"
#include "core/observed_geometry_utils.hpp"
#include <opencv2/imgproc.hpp>
#include <stdexcept>

namespace mark
{
    OriginalContourResult observeOriginalContour(const PreparedFrame &frame,
                                                 const WhiteComponent &component,
                                                 const CornerObservationBudget &budget)
    {
        OriginalContourResult result;
        cv::Mat gray, mask;
        cv::cvtColor(frame.original_image_, gray, cv::COLOR_BGR2GRAY);
        // threshold写独立图，严禁修改浅引用原图。
        cv::threshold(gray, mask, frame.original_white_threshold_, 255, cv::THRESH_BINARY);
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
        std::vector<cv::Point2d> mapped;
        for (auto p : component.contour_)
            mapped.push_back(frame.workToOriginal(p));
        if (mapped.size() < 3)
        {
            result.reason = "MISSING_COMPONENT_SUPPORT: 工作图轮廓无有效边界";
            return result;
        }
        size_t matches = 0;
        for (const auto &contour : contours)
        {
            if (contour.size() < 3)
                continue;
            std::vector<cv::Point2d> original(contour.begin(), contour.end());
            bool good = true;
            for (auto p : original)
                if (observed::boundaryDistance(p, mapped) >
                    budget.max_component_mapping_distance_px)
                {
                    good = false;
                    break;
                }
            if (!good)
                continue;
            for (auto p : mapped)
                if (observed::boundaryDistance(p, original) >
                    budget.max_component_mapping_distance_px)
                {
                    good = false;
                    break;
                }
            if (!good)
                continue;
            ++matches;
            result.contour = contour;
        }
        if (matches != 1)
        {
            result.contour.clear();
            result.reason = matches ? "AMBIGUOUS_COMPONENT_SUPPORT: 原图来源不唯一"
                                    : "MISSING_ORIGINAL_SUPPORT: 没有相符的原图轮廓";
            return result;
        }
        for (auto p : result.contour)
            if (p.x == 0 || p.y == 0 || p.x == frame.original_image_.cols - 1 ||
                p.y == frame.original_image_.rows - 1)
            {
                result.contour.clear();
                result.reason = "TRUNCATED_ORIGINAL_SUPPORT: 必要片真实原图边界截断";
                return result;
            }
        return result;
    }
}
