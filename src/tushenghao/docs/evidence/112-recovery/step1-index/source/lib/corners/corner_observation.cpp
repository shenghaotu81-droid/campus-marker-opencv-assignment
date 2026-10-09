// 输入assignment已确定的工作图组件，仅从当前原图量测；多来源不能排除时拒绝。
// 原实现裁ROI再closed=true会造边；这里从原图完整轮廓取证，无ROI人工闭合边。
#include "corners/corner_observation.hpp"
#include "core/observed_geometry_utils.hpp"
#include <opencv2/imgproc.hpp>
#include <stdexcept>

namespace mark
{
    // 原分割和双向关联的谓词不变；缓存只消除相同不可变输入上的重复计算。
    OriginalContourIndex::OriginalContourIndex(const PreparedFrame &frame, uint64_t frame_id,
                                               const CornerObservationBudget &budget)
        : frame_(frame), frame_id_(frame_id), budget_(budget)
    {
        if (frame.frame_id_ != frame_id)
            throw std::invalid_argument("ORIGINAL_INDEX_FRAME_MISMATCH");
    }

    uint64_t OriginalContourIndex::frameId() const
    {
        return frame_id_;
    }

    void OriginalContourIndex::prepare()
    {
        // 无待解析假设时不会调用 prepare；灰度、阈值、全图轮廓每帧至多一次。
        if (prepared_)
            return;
        cv::Mat gray, mask;
        cv::cvtColor(frame_.original_image_, gray, cv::COLOR_BGR2GRAY);
        // threshold写独立图，严禁修改浅引用原图。
        cv::threshold(gray, mask, frame_.original_white_threshold_, 255, cv::THRESH_BINARY);
        cv::findContours(mask, contours_, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
        prepared_ = true;
    }

    const OriginalContourResult &OriginalContourIndex::observe(const WhiteComponent &component)
    {
        auto found = components_.find(component.component_id_);
        if (found != components_.end())
            return found->second;
        prepare();
        auto &result = components_[component.component_id_];
        std::vector<cv::Point2d> mapped;
        for (auto p : component.contour_)
            mapped.push_back(frame_.workToOriginal(p));
        if (mapped.size() < 3)
        {
            result.reason = "MISSING_COMPONENT_SUPPORT: 工作图轮廓无有效边界";
            return result;
        }
        size_t matches = 0;
        for (const auto &contour : contours_)
        {
            if (contour.size() < 3)
                continue;
            std::vector<cv::Point2d> original(contour.begin(), contour.end());
            bool good = true;
            for (auto p : original)
                if (observed::boundaryDistance(p, mapped) >
                    budget_.max_component_mapping_distance_px)
                {
                    good = false;
                    break;
                }
            if (!good)
                continue;
            for (auto p : mapped)
                if (observed::boundaryDistance(p, original) >
                    budget_.max_component_mapping_distance_px)
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
            if (p.x == 0 || p.y == 0 || p.x == frame_.original_image_.cols - 1 ||
                p.y == frame_.original_image_.rows - 1)
            {
                result.contour.clear();
                result.reason = "TRUNCATED_ORIGINAL_SUPPORT: 必要片真实原图边界截断";
                return result;
            }
        return result;
    }

    OriginalContourResult observeOriginalContour(const PreparedFrame &frame,
                                                 const WhiteComponent &component,
                                                 const CornerObservationBudget &budget)
    {
        // wrapper 不保留状态：同 ID 的下一帧或修改过的 fixture 必须重新取证。
        OriginalContourIndex index(frame, frame.frame_id_, budget);
        return index.observe(component);
    }
}
