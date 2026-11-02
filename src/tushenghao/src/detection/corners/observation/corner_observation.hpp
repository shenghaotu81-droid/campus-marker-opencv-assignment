// 输入只读原图和明确assignment组件；输出完整原图连续轮廓，失败有原因。
#pragma once
#include "vision/types/detection/prepared_image.hpp"
#include "vision/config/detector_config.hpp"
#include <map>
#include <functional>
#include <string>

namespace mark
{
    struct OriginalContourResult
    {
        std::vector<cv::Point> contour;
        std::string reason;
    };

    struct OriginalContourSource
    {
        uint64_t frame_id{};
        std::function<const OriginalContourResult &(const WhiteComponent &)> observe;
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
        OriginalContourSource source() { return {frame_id_, [this](const WhiteComponent &c) -> const OriginalContourResult & { return observe(c); }}; }

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
