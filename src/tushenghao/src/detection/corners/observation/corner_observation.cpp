// 输入assignment已确定的工作图组件，仅从当前原图量测；多来源不能排除时拒绝。
// 原实现裁ROI再closed=true会造边；这里从原图完整轮廓取证，无ROI人工闭合边。
#include "corners/observation/corner_observation.hpp"
#include "vision/math/observed_geometry.hpp"
#include <opencv2/imgproc.hpp>
#include <stdexcept>

namespace mark
{
    // 原分割和双向关联的谓词不变；缓存只消除相同不可变输入上的重复计算。
    OriginalContourIndex::OriginalContourIndex(const PreparedFrame &frame)
        : frame_(frame),frame_id_(frame.frame_id_) {}

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

    OriginalContourResult OriginalContourIndex::atCorner(cv::Point2d corner) {
        prepare();
        OriginalContourResult result;double best=INFINITY;
        for(size_t i=0;i<contours_.size();++i) {
            if(contours_[i].size()<3)continue;
            const double distance=std::abs(cv::pointPolygonTest(contours_[i],corner,true));
            if(distance<best) {best=distance;result.component_id=i;result.contour=contours_[i];}
        }
        if(result.contour.empty())result.reason="MISSING_ORIGINAL_SUPPORT";
        for(auto p:result.contour) if(p.x==0 || p.y==0 || p.x==frame_.original_image_.cols-1 || p.y==frame_.original_image_.rows-1) {
            result.contour.clear();result.reason="TRUNCATED_ORIGINAL_SUPPORT";break;
        }
        return result;
    }

}
