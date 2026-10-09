// 输入assignment已确定的工作图组件，仅从当前原图量测；多来源不能排除时拒绝。
// 原实现裁ROI再closed=true会造边；这里从原图完整轮廓取证，无ROI人工闭合边。
#include "corners/corner_observation.hpp"
#include "corners/sandbox_profile.hpp"
#include "corners/sandbox_periodic.hpp"
#include "core/observed_geometry_utils.hpp"
#include <opencv2/imgproc.hpp>
#include <stdexcept>

namespace mark
{
    OriginalContourResult observeOriginalContour(const PreparedFrame &frame,
                                                 const WhiteComponent &component,
                                                 const CornerObservationBudget &budget, OriginalContourIndex &index)
    {
        sandbox::Scope profile(sandbox::Observe);
        OriginalContourResult result;
        if(auto it=index.matched.find(component.component_id_);it!=index.matched.end())return it->second;
        if(!index.ready){
        cv::Mat gray, mask;
        {sandbox::Scope profile(sandbox::Gray);cv::cvtColor(frame.original_image_, gray, cv::COLOR_BGR2GRAY);}
        // threshold写独立图，严禁修改浅引用原图。
        {sandbox::Scope profile(sandbox::Threshold);cv::threshold(gray, mask, frame.original_white_threshold_, 255, cv::THRESH_BINARY);}
        auto &contours=index.contours;
        {sandbox::Scope profile(sandbox::Contours);cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);}
        // 沙盒C2：每帧原图入口估计一次，绝不在失败角上重新拟合偏移。
        if(sandbox::modelMode()==2)index.parity_shift=sandbox::estimateFrameShift(contours);
        index.ready=true;
        }
        const auto &contours=index.contours;
        sandbox::Scope mapping_profile(sandbox::Mapping);
        std::vector<cv::Point2d> mapped;
        for (auto p : component.contour_)
            mapped.push_back(frame.workToOriginal(p));
        if (mapped.size() < 3)
        {
            result.reason = "MISSING_COMPONENT_SUPPORT: 工作图轮廓无有效边界";
            index.matched[component.component_id_]=result;return result;
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
            index.matched[component.component_id_]=result;return result;
        }
        for (auto p : result.contour)
            if (p.x == 0 || p.y == 0 || p.x == frame.original_image_.cols - 1 ||
                p.y == frame.original_image_.rows - 1)
            {
                result.contour.clear();
                result.reason = "TRUNCATED_ORIGINAL_SUPPORT: 必要片真实原图边界截断";
                index.matched[component.component_id_]=result;return result;
            }
        index.matched[component.component_id_]=result;return result;
    }
    // 历史内部入口保持语义：每次单独调用都用新索引，fixture改图/预算不会命中旧缓存。
    OriginalContourResult observeOriginalContour(const PreparedFrame &frame,const WhiteComponent &component,
      const CornerObservationBudget &budget){OriginalContourIndex index;return observeOriginalContour(frame,component,budget,index);}

}
