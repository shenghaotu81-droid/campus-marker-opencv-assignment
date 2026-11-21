// 检查运行期协方差有限、对称且正定，防止数值异常进入预测和更新。
// 协方差矩阵就是误差向量外积的期望（平均）P = E[e · eᵀ]
// 协方差矩阵怎么用：沿某个方向的方差 = u^T P u，u 是那个方向的单位向量。直观理解：uᵀPu = uᵀE[eeᵀ]u = E[(uᵀe)²] 把误差投影到 𝑢 方向上，平方，再平均
// P 是不确定度椭球，𝑢^𝑇𝑃𝑢 就是椭球在 𝑢 方向上的“半径平方”（沿那个方向的方差）。𝑢 是特征向量时，结果就是特征值；𝑢 是坐标轴时，结果就是对角线上的方差
// 展开（三维）：
//   Var(u) = ux^2*Pxx + uy^2*Pyy + uz^2*Pzz
//          + 2*ux*uy*Pxy + 2*ux*uz*Pxz + 2*uy*uz*Pyz
// 对角项（Pxx/Pyy/Pzz）：各方向自己的方差，按 u 在该方向分量的平方加权；
// 非对角项（Pxy/Pxz/Pyz）：两方向的协方差，按 u 在两方向分量的乘积加权，系数 2（P 对称，Pxy 与 Pyx 是同一项）。
// 当前R 是对角阵，非对角全是 0，只剩对角项：Var(u) = ux^2*2e-8 + uy^2*2e-8 + uz^2*2.5e-7。
// 沿该方向的误差大小（标准差）：sigma(u) = sqrt(u^T P u)。
// 另外两个常用组合：迹 tr(P) = Pxx+Pyy+Pzz（总方差）；马氏距离 e^T P^-1 e（一个误差向量相对协方差有多大，即 NIS）。
// --- 点积、投影与转置（u^T P u 的直观基础）---
// 点积：u^T e = |u||e|cosθ。u 是单位向量时，u^T e 就是 e 沿 u 方向的投影长度。
//   证明：余弦定理 |u-e|^2 = |u|^2+|e|^2-2|u||e|cosθ；展开 |u-e|^2 = |u|^2-2u^Te+|e|^2；两边一比即得。
//   直观：点积只留下 e 沿 u 的分量，垂直于 u 的部分点积为 0。
// 转置：只是翻转（列变行、矩阵沿主对角线翻），投影的几何意义属于点积本身；
//   转置是让点积能写成矩阵乘法 u^T e 的记号。矩阵转置 A^T 是 A 的伴随，满足 (Ax)·y = x·(A^T y)。
// u^T P u 的直观：P = E[e·e^T]，所以 u^T P u = E[(u^T e)^2]——误差投影到 u 方向，平方，再平均。
//   u^T 和 u 从两边夹住外积，只取沿 u 的那部分；u 是特征向量时结果是特征值，u 是坐标轴时结果是对角线方差。
// --- 协方差矩阵速查 ---
// P = E[e·e^T]：对角线是各方向方差，非对角是协方差；平均后对角阵当且仅当各方向误差不相关（R 假设 x/y/z 独立）。
// 沿方向 u 的方差：Var(u) = u^T P u = E[(u^T e)^2]（误差投影到 u，平方，再平均）；标准差 sigma = sqrt(u^T P u)。
// 点积 u^T e = |u||e|cosθ，u 为单位向量时即 e 沿 u 的投影长度；转置只是翻转，投影意义属于点积。
// 特征值：n×n 矩阵有 n 个；对角阵的特征值即对角线元素；特征向量是椭球主轴，特征值是沿轴方差；正定 = 全部特征值 > 0。
// 正定（所有特征值都 > 0）才够用：可逆只要求行列式 ≠ 0（所有特征值都不为 0）；正定则 P^-1 也正定、NIS 恒 ≥ 0、Cholesky 分解（P = LLᵀ,innovation 里算 NIS 用 Cholesky 解方程而不是显式求逆，数值更稳）存在。逆 A^-1 满足 A·A^-1 = I。有个特征值是 0（奇异），逆就不存在
#include "filter/covariance_validation.hpp"
#include <algorithm>
#include <limits>

namespace mark::tracking::detail
{
    // 协方差矩阵的“合格证检查”：一个矩阵要当协方差用，必须同时满足三条——有限、对称、正定
    // 卡尔曼的数学（Cholesky 分解、求逆）要求协方差矩阵必须是对称正定的，R 或 P 一旦不是，滤波器就算崩了
    // 坏的协方差不让进 predict / correct
    bool positive_definite(const cv::Mat &m)
    {
        // 形状关：矩阵不能为空，类型必须是 CV_64F（double），必须是方阵（行数 = 列数），所有元素必须有限（checkRange）；有一条不过就返回 false。
        // cv::checkRange(m)：bool checkRange(InputArray a, ...)，检查矩阵所有元素是不是有限值（不是 NaN/±∞）。全有限返回 true，有一个不有限就 false
        if (m.empty() || m.type() != CV_64F || m.rows != m.cols || !cv::checkRange(m))
            return false;
        
        // 对称关：算 m - m.t()（矩阵减去它的转置）的最大绝对值——对称矩阵减转置应该是 0，但浮点计算有舍入误差，所以跟一个容差 roundoff 比
        // 容差 = 机器精度 × 行数 × 矩阵量级（scale 是矩阵里最大的绝对值（量级），至少取 1）
        // 浮点舍入误差的量级（容差取的就是“浮点运算自己会带进来的误差水平”）：epsilon（约 2.22e-16）：double 的机器精度，一次运算的相对误差就是这个量级；scale（矩阵里最大的绝对值）：误差是相对的——数越大，绝对误差越大；m.rows（行数）：矩阵是经过一系列加法/乘法算出来的，每加一次就多攒一点误差，攒的次数大概跟维度成正比（数值线性代数里的标准误差界：算一个 n 维矩阵，误差大约是 n 倍的单步误差）
        // std::max(1.0, ...)：矩阵特别小的时候，量级 < 1，容差不能跟着缩到比 epsilon × 行数 还小，不然连一个小矩阵自己的舍入误差都容不下
        // cv::norm 有两种用法：两个输入：cv::norm(a, b) = a 和 b 的差的范数。默认 NORM_L2，就是两点之间的欧氏距离（sqrt(Σ(aᵢ−bᵢ)²)）
        //                     一个输入：cv::norm(m) = m 自己的范数（大小）；normType 决定怎么算：NORM_L2（默认）：欧氏长度，sqrt(Σxᵢ²)；NORM_L1：绝对值之和；NORM_INF：最大的绝对值。
        // cv::norm(m, cv::NORM_INF)：double norm(InputArray src, int normType)，NORM_INF 是无穷范数 = 所有元素里最大的绝对值；这里两次用：一次算矩阵量级 scale，一次算 m - m.t() 的最大不对称量
        const double scale = std::max(1.0, cv::norm(m, cv::NORM_INF));
        // std::numeric_limits<double>::epsilon()：double 的机器精度，约 2.22e-16，就是 1.0 和下一个能表示的 double 之间的差。拿它乘行数和量级，得到“浮点舍入能容忍的误差”
        const double roundoff = std::numeric_limits<double>::epsilon() * m.rows * scale;
        // m.t()：Mat t() const，返回矩阵的转置（不改原矩阵）
        if (cv::norm(m - m.t(), cv::NORM_INF) > roundoff)
            return false; // 不对称超过容差就返回 false
        
        // 正定关：算特征值
        // 对称矩阵的特征值都是实数，cv::eigen 按从大到小排，所以最后一个（m.rows - 1 号）就是最小的
        cv::Mat eigenvalues;
        // cv::eigen(m, eigenvalues)：bool eigen(InputArray src, OutputArray eigenvalues, ...)，算对称矩阵的特征值（降序排），成功返回 true。eigenvalues 是个列向量，第 m.rows - 1 个就是最小特征值
        // eigenvalues.at<double>(i)：Mat::at<T>(int i)，带类型检查的元素访问，取第 i 个元素。这里取最后一个特征值，跟 0 比大小
        // 一个方阵 A，如果有个非零向量 v 满足：𝐴·𝑣=𝜆·𝑣 v 叫特征向量，λ 叫特征值。意思是矩阵 A 作用在 v 上，结果只是把 v 拉长/缩短 λ 倍，方向不变。
        // 对称矩阵正定 = 所有特征值都 > 0。等价的说法：对任何非零向量 x，都有 𝑥𝑇𝐴𝑥>0
        // 对协方差矩阵几何意义（二维）：特征向量 = 椭圆主轴的方向（不确定度最大的方向、最小的方向）；特征值 = 沿那个主轴的方差（半轴长的平方）；正定 = 所有特征值都 > 0，椭圆在每个方向上都有正的伸展，是个实实在在有面积的椭圆
        // 特征向量是“不确定度椭球”的主轴方向，特征值是沿那个轴的方差。最大特征值 = 最弥散的方向的方差，最小特征值 = 最收敛的方向的方差
        // 正定就是“每个方向的不确定度都是正的”——没有哪个方向方差是 0（过度自信）或负数（不可能）。正定才能求逆、才能做 Cholesky 分解，卡尔曼的数学才转得动。一旦不正定，滤波器就算崩了（这个矩阵画出来的不确定度区域，是不是一个每个方向都有正体积的正常椭球）
        // n×n 的矩阵就有 n 个特征值（按重数算）。3×3 的协方差矩阵算出来就是 3 个特征值，对应椭球的 3 个主轴方向的方差；6×6 的 P_ 就是 6 个
        // 对角阵的特征值就是对角线上的那几个，特征向量就是坐标轴本身
        return cv::eigen(m, eigenvalues) && eigenvalues.at<double>(m.rows - 1) > 0; // 最小特征值 > 0，矩阵就是正定的，返回 true；否则 false
    }
}
