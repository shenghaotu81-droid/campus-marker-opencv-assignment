// 由粗角查询当前原图的最近真实轮廓，供两条邻边支持提取。
// 从完整原图提取真实轮廓，避免 ROI 截边产生人工闭合边。
#include "corners/corner_observation.hpp"
#include <cmath>
#include <opencv2/imgproc.hpp>

// 修复一下编辑器的报错，让它找的到(脆)
#include "corner_observation.hpp"

namespace mark::detector
{
    // 同一帧按需建立轮廓索引，四角查询复用同一份分割结果。
    // 构造函数：把 frame 的引用存进 frame_
    // 懒准备：第一次查询 at_corner 时才 threshold+findContours，之后复用 contours_
    OriginalContourIndex::OriginalContourIndex(const PreparedFrame &frame) : frame_(frame)
    {
    }

    // prepare()
    void OriginalContourIndex::prepare()
    {
        // 无待解析四边形时不会调用 prepare；灰度、阈值、全图轮廓每帧至多一次。
        if (prepared_) // 开关——每帧至多跑一次，第二次调用直接返回
            return;
        
        cv::Mat gray, mask;
        // 原图转灰度，再用原图阈值（original_white_threshold_是 PreprocessConfig.threshold_ 里 200）二值化，写进独立的 mask——原图是浅引用只读，绝不能原地改
        cv::cvtColor(frame_.original_image_, gray, cv::COLOR_BGR2GRAY);
        // threshold写独立图，严禁修改浅引用原图。
        cv::threshold(gray, mask, frame_.original_white_threshold_, 255, cv::THRESH_BINARY);

        // findContours 找外轮廓，存进 contours_
        // 这里是 CHAIN_APPROX_NONE（保留每一个边界点），跟工作图的 CHAIN_APPROX_SIMPLE（只留折线端点）不一样
        // 角点拟合要拿轮廓上的点去拟合直线，点越密拟合越准，压缩了就没法用
        cv::findContours(mask, contours_, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);

        // 标记已准备好
        prepared_ = true;
    }

    // at_corner()
    OriginalContourResult OriginalContourIndex::at_corner(cv::Point2d corner)
    {
        prepare(); // 先调 prepare() 触发懒准备（已准备好就是空操作）
        OriginalContourResult result;
        double best = INFINITY;
        // 遍历所有原图轮廓，找离这个角点最近的那一条
        for (size_t i = 0; i < contours_.size(); ++i)
        {
            // 少于 3 个点的轮廓跳过（成不了形状）
            if (contours_[i].size() < 3)
                continue;
            // pointPolygonTest 量角点到轮廓的距离，取绝对值（只关心远近，不关心角点在轮廓内还是外）
            // cv::pointPolygonTest(contour, pt, measureDist) 会遍历轮廓的每一条线段，计算点到这条线段的最近距离（先判断点在轮廓内还是外，再算点到最近轮廓边的最短距离）：
            // 如果垂足落在线段内，就取点到直线的垂直距离；如果垂足落在线段外，就取点到最近端点的距离；所有边里取最小的那个作为最终距离
            // measureDist 为 true 时，返回的是带符号的欧氏距离；measureDist 为 false，就不算具体距离，只返回 +1 / 0 / -1，判断点在内部、边上还是外部
            const double distance = std::abs(cv::pointPolygonTest(contours_[i], corner, true));
            // 最近的就记下来（距离、component_id_、轮廓指针）
            if (distance < best)
            {
                best = distance;
                result.component_id_ = i;
                result.contour_ = &contours_[i];
            }
        }
        // 一条都没找到（contour_ 还是空；空指针（nullptr）就是 false，非空指针就是 true）
        if (!result.contour_)
            result.reason_ = "MISSING_ORIGINAL_SUPPORT";
        // 找到了，但轮廓上有任何一个点贴着图像边（x==0、y==0、x==cols-1、y==rows-1）（说明这条轮廓被画面边缘截断了、不完整，作废）
        // 一个完整的白块，四周全是黑背景，它的轮廓点都严格在图像内部（x 在 1 到 cols-2 之间，y 在 1 到 rows-2 之间），碰不到边
        if (result.contour_)
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
