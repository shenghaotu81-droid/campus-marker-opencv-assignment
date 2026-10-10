// 输入只读原图和明确assignment组件；输出完整原图连续轮廓，失败有原因。
#pragma once
#include "core/prepared_frame.hpp"
#include "mark/detector_config.hpp"
#include <map>
#include <string>

namespace mark
{
    struct OriginalContourResult
    {
        std::vector<cv::Point> contour;
        std::string reason;
    };

    // 原来每个角/父假设都重新分割原图；一次 decode 共用索引，按组件 ID 缓存关联。
    // frame、组件和预算在调用期只读；帧号显式绑定来源，索引不得跨 decode 留存。
    class OriginalContourIndex
    {
      public:
        OriginalContourIndex(const PreparedFrame &frame, uint64_t frame_id,
                             const CornerObservationBudget &budget);
        const OriginalContourResult &observe(const WhiteComponent &component);
        uint64_t frameId() const;

      private:
        void prepare();
        const PreparedFrame &frame_;
        const uint64_t frame_id_;
        const CornerObservationBudget budget_;
        bool prepared_{false};
        std::vector<std::vector<cv::Point>> contours_;
        std::map<size_t, OriginalContourResult> components_;
    };

    // 独立调用仍创建新索引，fixture 换原图/预算后不会读到上次缓存。
    OriginalContourResult observeOriginalContour(const PreparedFrame &, const WhiteComponent &,
                                                 const CornerObservationBudget &);
}
