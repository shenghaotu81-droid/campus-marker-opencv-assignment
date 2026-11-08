// 由粗角查询当前原图的最近真实轮廓，供两条邻边支持提取。
// 从完整原图提取真实轮廓，避免 ROI 截边产生人工闭合边。
#include "corners/corner_observation.hpp"
#include <cmath>
#include <opencv2/imgproc.hpp>

namespace mark::detector
{
    // 同一帧按需建立轮廓索引，四角查询复用同一份分割结果。
    OriginalContourIndex::OriginalContourIndex(const PreparedFrame &frame)
        : frame_(frame)
    {
    }

    void OriginalContourIndex::prepare()
    {
        // 无待解析四边形时不会调用 prepare；灰度、阈值、全图轮廓每帧至多一次。
        if (prepared_)
            return;
        cv::Mat gray, mask;
        cv::cvtColor(frame_.original_image_, gray, cv::COLOR_BGR2GRAY);
        // threshold写独立图，严禁修改浅引用原图。
        cv::threshold(gray, mask, frame_.original_white_threshold_, 255, cv::THRESH_BINARY);
        cv::findContours(mask, contours_, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
        prepared_ = true;
    }

    OriginalContourResult OriginalContourIndex::at_corner(cv::Point2d corner)
    {
        prepare();
        OriginalContourResult result;
        double best = INFINITY;
        for (size_t i = 0; i < contours_.size(); ++i)
        {
            if (contours_[i].size() < 3)
                continue;
            const double distance = std::abs(cv::pointPolygonTest(contours_[i], corner, true));
            if (distance < best)
            {
                best = distance;
                result.component_id_ = i;
                result.contour_ = &contours_[i];
            }
        }
        if (!result.contour_)
            result.reason_ = "MISSING_ORIGINAL_SUPPORT";
        if (result.contour_)
            for (auto p:*result.contour_)
                if (p.x == 0 || p.y == 0 || p.x == frame_.original_image_.cols - 1 || p.y == frame_.original_image_.rows - 1)
                {
                    result.contour_ = nullptr;
                    result.reason_ = "TRUNCATED_ORIGINAL_SUPPORT";
                    break;
                }

        return result;
    }

}
