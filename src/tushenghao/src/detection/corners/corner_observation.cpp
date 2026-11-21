// 读取粗角附近的原图外轮廓，用于精修。
#include "corners/corner_observation.hpp"
#include <cmath>
#include <opencv2/imgproc.hpp>

namespace mark::detector
{
    OriginalContourIndex::OriginalContourIndex(const PreparedFrame &frame) : frame_(frame)
    {
    }

    void OriginalContourIndex::prepare()
    {
        // 同一帧的四角共用一次原图分割。
        if (prepared_)
            return;

        cv::Mat gray, mask;

        cv::cvtColor(frame_.original_image_, gray, cv::COLOR_BGR2GRAY);

        cv::threshold(gray, mask, frame_.original_white_threshold_, 255, cv::THRESH_BINARY);

        // 保留逐点轮廓，供连续边支持和固定索引切线窗使用。
        cv::findContours(mask, contours_, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);

        prepared_ = true;
    }

    OriginalContourResult OriginalContourIndex::at_corner(cv::Point2d corner)
    {
        prepare();
        OriginalContourResult result;
        double best = INFINITY;

        // 按点到轮廓的绝对距离找最近角片。
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
            // 触边的角片可能被截断，不能用作完整支持。
            for (auto p : *result.contour_)
                if (p.x == 0 || p.y == 0 || p.x == frame_.original_image_.cols - 1 ||
                    p.y == frame_.original_image_.rows - 1)
                {
                    result.contour_ = nullptr;
                    result.reason_ = "TRUNCATED_ORIGINAL_SUPPORT";
                    break;
                }

        return result;
    }
}
