// 把工作图四边形映射到原图角片，逐角拟合并发布可供位姿使用的浮点四角。
// 检测管线总调度：输入帧 → 预处理 → 四边形匹配 → 原图逐角拟合，输出可供位姿使用的浮点四角
// “纯干活的管线总调度”
#include "pipeline/decode_stage.hpp"
#include "preprocess/preprocess.hpp"
#include "geometry/geometry_observation.hpp"
#include "corners/corner_edge_fit.hpp"
#include <set>
#include "corners/corner_observation.hpp"

// 修复一下编辑器的报错，让它找的到(脆)
#include "decode_stage.hpp"
#include "../preprocess/preprocess.hpp"
#include "../geometry/geometry_observation.hpp"
#include "../corners/corner_edge_fit.hpp"
#include "../corners/corner_observation.hpp"

namespace mark::detector
{
    // 原图轮廓逐角拟合，确保四个物理角使用不同组件。
    // 在原图上逐个精修 4 个物理角，并且保证 4 个角用的是 4 个不同的轮廓组件；失败就整个返回（不给部分结果）
    CornerResolution resolve_observed_corners(const CornerPoints &physical, OriginalContourIndex &index)
    {
        CornerPoints measured; // 装 4 个精修后的角点（输出）
        std::set<size_t> used; // 记已经用过的轮廓组件 id，用来查重——std::set 里一个值只能出现一次
        // 逐角循环
        for (size_t i = 0; i < 4; ++i)
        {
            // 找离这个角最近的原图轮廓
            auto source = index.at_corner(physical[i]); // OriginalContourIndex 原图轮廓索引，at_corner 找角附近的轮廓
            if (!source.contour_)
                return {{}, source.reason_}; // 找不到（contour_ 为空）就直接返回失败，原因就是 at_corner 给的（MISSING_ORIGINAL_SUPPORT 或 TRUNCATED_ORIGINAL_SUPPORT）
            
            // 查重：used.insert(component_id_) 往集合里插这个轮廓的 id；insert返回一对值（std::pair<迭代器, bool>，.first：一个迭代器，指向这个元素（新插进去的就是新元素，已经在的就是原来那个）），.second 是“这次是不是新插进去的”
            if (!used.insert(source.component_id_).second)
                return {{}, "REUSED_CORNER_COMPONENT"}; // 如果是 false，说明这个 id 已经被前面的角用过了，两个角共用一个轮廓，返回 "REUSED_CORNER_COMPONENT"
            
            std::string reason_;
            // 真正算精修角点
            // 参数是：这条轮廓、这个角的粗位置、以及它的两个邻角 {physical[(i+3)%4], physical[(i+1)%4]}（前一个和后一个物理角，绕一圈取）
            // 两个邻角定出交于这个角的两条边，拟合就沿着这两条边算
            auto corner = fit_observed_corner(*source.contour_, physical[i], {physical[(i + 3) % 4], physical[(i + 1) % 4]}, reason_); // 逐角拟合，真正算精修角点
            if (!corner)
                return {{}, reason_}; // 失败就返回 reason_
            
            // 把拟合好的角点存进输出数组
            measured[i] = *corner;
        }
        // 4 个角全精修成功，返回 {精修后的 4 个角，空原因}；大括号里的东西由返回类型来解释，类型名不用重复写
        return {measured, {}};
    }

    // float 舍入后复核凸环和图像边界，再向位姿模块发布屏幕角。
    std::optional<Detection> publish_float_detection(const CornerPoints &points, cv::Size size, const CornerConfig &config, std::string &reason_)
    {
        // 舍入（下游要的格式是 float，这里先舍入）
        // 把精修出来的 double 角点逐个转成 float（Point2f）
        // 注意 rounded 本身还是 CornerPoints（Point2d 数组），所以 float 值又存回 double
        // 存的是“舍入后那个 float 值的精确 double 表示”，后面验证的是这个最终要存的值，不会再有舍入意外
        CornerPoints rounded;
        for (size_t i = 0; i < 4; ++i)
            // 实际上两步：先 double→float 舍入，再 float→double 存回 rounded[i]
            rounded[i] = cv::Point2f(points[i]); // 转换构造：cv::Point2f(一个 Point2d)；把 double 坐标转成 float——这就是“舍入”本身，会丢精度（double 的小数位被截到 float 的精度）
        
        // 再排角、再验证（凸性、边界）
        // 对舍入后的点再调一次 order_screen_corners（第一次排角是在 decode_stage 里对粗角（MatchQuad）做的，这次是对精修+舍入后的点做的）
        // 舍入可能改变数值，第一遍的凸性/有限性保证不能直接套到舍入后的值上，所以重新验一遍
        auto ordered = order_screen_corners(rounded, reason_);
        if (!ordered)
            return {}; // 失败就返回空，原因已经由 order_screen_corners 填好

        // 建 Detection
        // 注意这里的角点经过 order_screen_corners 后是屏幕顺序 [左上，右上，右下，左下]，orientation_ 的作用就是让位姿模块能反查回去（estimate_mark 里 pixels[i] = d.corners_[d.orientation_[i]]，物理角 i 就取屏幕顺序里第 orientation_[i] 个点）
        Detection d;
        d.orientation_ = config.physical_to_screen_; // d.orientation_ 记下物理→屏幕的映射

        // 逐点边界检查 + 填入
        for (size_t i = 0; i < 4; ++i)
        {
            auto p = (*ordered)[i];
            if (p.x < 0 || p.y < 0 || p.x >= size.width || p.y >= size.height) // 逐个检查角点在不在图像里——size 是 decode_stage 传进来的原图尺寸（frame.original_image_.size()）
            {
                reason_ = "CORNER_OUT_OF_IMAGE";
                return {}; // x 要在 [0, width)、y 要在 [0, height)，出界就是 "CORNER_OUT_OF_IMAGE"，返回空
            }
            d.corners_[i] = cv::Point2f(p); // 没问题就存进 d.corners_[i]；转换构造：cv::Point2d(一个 Point2f)：float 转 double，是精确的，不丢东西
        }
        // 四个都通过，return一个完整的 Detection
        return d;
    }

    // 拿 match_quads 给的粗四角（MatchQuad），逐个精修成能给位姿用的 Detection
    FrameResult decode_stage(const PreparedFrame &frame, const std::vector<MatchQuad> &matches, const CornerConfig &config)
    {
        // 开头建 result 和 OriginalContourIndex
        FrameResult result;
        result.status_ = Status::NOT_DETECTED;
        // index 只建一次：OriginalContourIndex 在循环外建一次，所有 match 共用——原图的阈值化+找轮廓每帧只做一次，不会每个 match 重做一次
        OriginalContourIndex index(frame); // 原图轮廓索引，at_corner 找角附近的轮廓

        // 中间逐个 match 走四步（排角 → 转物理顺序 → 原图精修 → 发布）
        // 失败只隔离到单个 match(四步里任何一步失败（排角失败、精修失败、发布失败），都只是记一条 diagnostic、continue 跳过这个 match，继续处理下一个，不影响整帧;最后只要有一个检测成功，status_ 就是 DETECTED)
        for (const auto &match:matches)
        {
            //排角；排成屏幕顺序，失败就记诊断、continue
            std::string reason_;
            auto screen = order_screen_corners(match, reason_); // 把 4 个点排成统一的屏幕顺序
            if (!screen)
            {
                result.diagnostics_.push_back(reason_);
                continue;
            }

            // 转物理顺序：physical_to_screen_ 映射，把屏幕顺序重排成物理顺序
            // 把屏幕顺序转成物理顺序（把 [左上，右上，右下，左下] 的屏幕顺序，重排成 [物理 0，1，2，3] 的物理顺序（MARK 自己的身份（哪个角是 MARK 的 0 号角）））
            CornerPoints physical; // 声明一个 4 点数组，装物理顺序的角点
            for (size_t p = 0; p < 4; ++p)
                physical[p] = (*screen)[config.physical_to_screen_[p]]; // (*screen) 是把 optional 解开拿到排好序的 4 个点；physical_to_screen_ 是配置里的固定映射（“物理角 p 在屏幕顺序里是第几个”）
            
            // 原图精修：在原图上把每个角精修到浮点位置，失败就记诊断、continue
            auto measured = resolve_observed_corners(physical, index); // 在原图上把每个角精修到浮点位置
            if (!measured.corners_)
            {
                result.diagnostics_.push_back(measured.reason_);
                continue;
            }

            // 发布 float 检测：舍入复核后建成 Detection，成功进 detections_、失败进 diagnostics_
            auto d = publish_float_detection(*measured.corners_, frame.original_image_.size(), config, reason_); // 舍入复核（凸环+图像边界），建成 Detection 发布
            if (d)
                result.detections_.push_back(*d);
            else
                result.diagnostics_.push_back(reason_);
        }

        // 结尾有检测就置 DETECTED 返回
        if (!result.detections_.empty())
            result.status_ = Status::DETECTED;
        return result;
    }

    // decode_stage 的总调度：预处理 → 四边形匹配 → 原图逐角拟合，输出可供位姿使用的浮点四角！！！
    FrameResult run_decode_pipeline(const FrameInput &input, const DetectorConfig &config)
    {
        auto frame = preprocess(input, config.preprocess_);
        auto components = extract_white_components(frame, config.geometry_);
        return decode_stage(frame, match_quads(frame, components, config.geometry_), config.corner_);
    }

}
