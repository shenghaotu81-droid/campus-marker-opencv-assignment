// 多个角点假设打架时，它当裁判
/*Block 3:step5
Step 3 可能对同一个目标给出多个 CornerMeasurement（不同解释）。Step 5 拿着这些假设问三个问题：
1.是不是同一个东西？ → geometry_consistent_。无标签比四个点，距离够近就认作同一几何。
2.方向定得下来吗？ → orientation_unique_。几何一样但 P0 位置不同 = 方向有歧义。
3.留哪个？ → retained_measurements_。不平均，不创造新点，选残差最小的那个已有假设。
它不检测、不排序，只做"假设归并"。
*/
#pragma once

#include <vector>

#include "corners/corner_types.hpp"

namespace mark
{

    /**
     * @brief 语义归并接口
     *
     * 负责处理多个 CornerMeasurement 之间的解释关系。
     *
     * 它回答：
     *
     * 1. 多个几何测量是否描述同一个目标；
     * 2. 当前方向是否可以唯一确定；
     * 3. 哪些测量假设应该保留。
     *
     * 它不负责：
     *
     * - 从图像中检测角点；
     * - 拟合边和计算交点；
     * - 屏幕顺序排序。
     *
     * 输入：
     *
     * CornerMeasurement
     *      ↓
     * 语义归并
     *      ↓
     * SemanticResolution
     */
    SemanticResolution resolveSemantics(
        const std::vector<CornerMeasurement> &measurements,
        bool search_truncated,
        const CornerConfig &config);

} // namespace mark