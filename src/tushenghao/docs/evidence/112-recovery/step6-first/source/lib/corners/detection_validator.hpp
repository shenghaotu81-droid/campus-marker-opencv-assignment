/* 最终质检员的"上岗证"。
这个 hpp 只声明一个函数 validateDetectionGeometry，不干活，只定接口：

输入：CornerMeasurement（4 个物理角）+ ScreenOrder（屏幕顺序映射）+ 原图尺寸 + 配置
输出：DetectionValidation（过/不过 + 原因）
干啥：检查 4 个点是不是合法凸四边形、映射对不对、有没有出界
不干：不重新检测、不改角点身份、不创建 Detection、不 decode

实现在 cpp 里，hpp 只是告诉别人"我有这个功能，签名长这样"。
*/
#pragma once

#include "corners/corner_types.hpp"

namespace mark
{

    /**
     * @brief 最终几何质检接口
     *
     * 负责检查：
     *
     * - 四角测量结果是否满足几何约束；
     * - 屏幕排序映射是否有效；
     * - 当前角点是否可以作为最终检测结果依据。
     *
     * 输入：
     *
     * CornerMeasurement
     *      +
     * ScreenOrder
     *
     * 输出：
     *
     * DetectionValidation
     *
     * 注意：
     *
     * 这里只做验证。
     *
     * 不负责：
     *
     * - 从图像重新检测角点；
     * - 修改物理角身份；
     * - 重新排序屏幕点；
     * - 创建 Detection 对象；
     * - 调用 decode。
     */
    DetectionValidation validateDetectionGeometry(
        const CornerMeasurement &measurement,
        const ScreenOrder &order,
        cv::Size original_size,
        const CornerConfig &config);

} // namespace mark