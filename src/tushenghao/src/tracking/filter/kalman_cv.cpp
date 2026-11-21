// 位置和速度按匀速模型预测，位置量测通过创新和 Joseph 更新推进协方差。
// 潜在提升点：自适应 Q（NIS）；距离相关的 R
// 流程概括：每帧先用运动模型预测出先验状态 x⁻ 和协方差 P⁻，再把量测与预测的残差按卡尔曼增益 K （由两部分不确定性（权衡）决定：预测的不确定性P⁻ + 量测的不确定性R）加权、修正为后验 x̂ 和 P，逐帧循环
// 位置进滤波器、朝向走旁路
// 量测 3 维（位置），状态 6 维（位置 + 速度）；传感器只给位置，速度是滤波器从一连串位置变化里推出来的（靠运动模型 + P 的位置-速度交叉项）
// ！！！卡尔曼滤波数学速查 (CvFilter: 6 维常速度模型, 只量位置)
// 状态 x = [p(3), v(3)], 协方差 P = E[e·eᵀ] (6×6)。
// 沿单位方向 u 的方差: Var(u) = uᵀPu; NIS = eᵀP⁻¹e 是马氏距离平方。
//
// 5 个标准公式 (predict = 1-2, correct = 3-5):
//   (1) x̂⁻ = F·x̂        (2) P⁻ = F·P·Fᵀ + Q
//   (3) K = P⁻·Hᵀ·S⁻¹, S = H·P⁻·Hᵀ + R
//   (4) x̂ = x̂⁻ + K·(z − H·x̂⁻)
//   (5) P = (I − K·H)·P⁻  (代码用 Joseph 形式, 数值更稳)
//
// F (常速度模型): F = [I₃, dt·I₃; 0, I₃]; 新位置 = 旧位置 + 速度×dt, 速度不变。
// Q (白噪声加速度带来的新不确定性（模型不完美）, 强度 q, 单位 m²/s³): 每轴 Q = q·[[dt³/3, dt²/2],[dt²/2, dt]],
//   位置块 q·dt³/3, 交叉 q·dt²/2, 速度块 q·dt。Q 由 q 和 dt 唯一确定。
// H = [I₃ 0₃]: 只量位置, 速度靠 P 的位置-速度交叉项间接修正。H 是状态到量测的映射，定义传感器量的是状态里的哪部分（这里是位置）。
//
// innovation: 残差 = z − H·x̂⁻; S = position_block(P⁻) + R (预测方差 + 量测噪声);
//   NIS = 残差ᵀ·S⁻¹·残差 ~ χ²₃; 白化: S = L·Lᵀ, y = L⁻¹·残差 ~ N(0,I), NIS = yᵀy。
//   NIS 只用于量测关联排序, 不设拒绝门槛。
//
// Joseph 形式: P = (I−K·H)·P⁻·(I−K·H)ᵀ + K·R·Kᵀ, 数学上等于公式 (5),
//   两项都对称半正定, 和一定对称正定, 不会因舍入误差丢对称性。
//
// bootstrap: v = (p1−p0)/dt; P 位置块 = r1, 速度块 = (r0+r1)/dt², 交叉 = r1/dt,
//   速度对角再加 q·dt/3 (平均速度当瞬时速度的代价)。
#include "filter/covariance_validation.hpp"
#include "filter/kalman_cv.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace mark::tracking::detail
{
    namespace
    {
        // 把矩阵对称化再验正定，不合法就抛错
        // checked_covariance 只做对称化，P 非正定就抛错，不加隐藏的 epsilon 或对角“救援”噪声
        // 这里只处理浮点舍入对称性，不给物理预算添加隐形epsilon或对角“救场”噪声。（不偷偷修）
        Mat6 checked_covariance(const cv::Mat &matrix)
        {
            // 对称化：(matrix + matrix.t()) * 0.5，矩阵跟自己的转置取平均，强制精确对称
            // P_ 是手填的（P_(i,j+3) 和 P_(j+3,i) 各写一次），浮点运算可能让两边差个 1e-16，对称化把它抹平
            const cv::Mat symmetric = (matrix + matrix.t()) * 0.5;
            // 验正定：对称化完调 positive_definite 验，不合法就抛 TRACK_COVARIANCE_NOT_POSITIVE_DEFINITE
            if (!positive_definite(symmetric))
                throw std::runtime_error("TRACK_COVARIANCE_NOT_POSITIVE_DEFINITE");
            Mat6 result;
            // 转成 Mat6：copyTo 把数据拷进一个 Mat6 里返回
            // 输入是 cv::Mat（因为 bootstrap 里 P_ 是 Mat6，传进来时先包成 Mat），输出是定长 Mat6，跟 P_的类型对上。
            // Mat::copyTo：void Mat::copyTo(OutputArray m) const，把矩阵数据拷到目标里
            // 这里目标是 cv::Mat(result, false)——一个指向 result（Mat6）存储的 Mat 头（不拷数据，只包一下），所以 copyTo 是把 symmetric 的数据写进 result 的 6×6 存储里
            // cv::Mat 从 Matx 构造的签名：Mat(const Matx<_Tp, m, n>& m, bool copyData = false);
            // copyData 控制拷不拷贝数据：false（默认）：Mat 头只指向 Matx 已有的存储，不拷数据；true：Mat 自己另开一块内存，把 Matx 的数据拷过去，两边独立
            // 把 symmetric 的数据拷进 result 的存储里：靠 Mat 头跟 result 共享内存，copyTo 写 Mat 头就等于写 result。
            // Matx → Mat 可以，Mat → Matx 不可以；用copyTo()手动转换（绕过 Matx 没有转换构造这个限制，用 Mat 的机制完成拷贝）
            symmetric.copyTo(cv::Mat(result, false));
            return result;
        }

    } // namespace

    // 检查一个三维向量“是不是正常的数”
    bool finite_vector(const cv::Vec3d &v)
    {
        // 三个分量都得是有限值（不是 NaN，也不是 ±∞），三个都满足才返回 true，有一个不满足就返回 false
        return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); // std::isfinite（<cmath>）：判断一个浮点数是不是有限值。有限返回 true；NaN 或 ±∞ 返回 false；isfinite 是 C++ 标准库的浮点分类函数，跟 isnan（是不是 NaN）、isinf（是不是无穷）是一家；isfinite 等于“既不是 NaN 也不是无穷”
    }

    // 取 Mat6 左上 3×3 块（位置–位置协方差块），返回 cv::Matx33d
    // 状态 x = [位置，速度]，P_ 是 6×6，分四块：左上是位置方差，右上/左下是位置–速度交叉，右下是速度方差。这个 helper 只抠左上那块出来
    // 用在 innovation 里算 S：H = [I₃ 0₃] 只量位置，H·P⁻·Hᵀ 正好就是 P_ 的左上 3×3，所以 s = position_block(P_) + r
    cv::Matx33d position_block(const Mat6 &p)
    {
        cv::Matx33d out;
        // 双重循环逐个拷；搬运
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                out(i, j) = p(i, j);

        return out;
    }

    // 第一帧，用第一个量测把滤波器搭起来；轨迹建完还“未确认”，速度是猜的（一个量测算不出速度），要等第二帧 bootstrap 拿两个量测算出真正的速度才确认
    // 首帧速度只是零均值先验，不对外发布“测得零速”；方差来自 profile 的初速模型。
    // 三个输入：p 是第一个量测的位置，r 是它的噪声 R，vv 是初始速度方差（profile 里的 initial_velocity_variance_m2ps2_）
    void CvFilter::initialize(const cv::Vec3d &p, const cv::Matx33d &r, double vv)
    {
        // 先验算：p 必须有限、r 必须是正定矩阵，否则抛 TRACK_BAD_INITIAL_STATE
        // cv::Mat(r)——用 Matx 造一个 Mat 头 template<typename _Tp, int m, int n> Mat(const Matx<_Tp, m, n>& m);
        // 造一个 cv::Mat 的头，指向 r（Matx33d）的数据，不拷数据；Mat 是通用矩阵类型，Matx 是固定尺寸类型，这个构造就是让通用 Mat 的代码能读固定尺寸 Matx 的数据
        if (!finite_vector(p) || !positive_definite(cv::Mat(r))) // 防坏数据的门——拿坏的量测初始化，滤波器一开始就是错的
            throw std::runtime_error("TRACK_BAD_INITIAL_STATE");

        // 状态和协方差先清零，再往里填
        // all(v) 是“全填成 v”，跟 zeros()（全 0）、ones()（全 1）是一家
        // x_ 用 all(0)、下面 P_ 用 zeros()，效果一样，只是写法不同
        x_ = Vec6::all(0); // cv::Vec::all()：template<typename _Tp, int cn> static Vec<_Tp, cn> all(_Tp v); 静态方法，返回一个所有元素都是 v 的向量。这里 Vec6::all(0) 就是 [0, 0, 0, 0, 0, 0]
        // 跟 eye()（单位阵）、ones()（全 1）是一家，都是静态工厂方法，直接返回一个现成的矩阵，不用先声明再赋值
        P_ = Mat6::zeros(); // cv::Matx::zeros()：template<typename _Tp, int m, int n> static Matx<_Tp, m, n> zeros(); 静态方法，返回一个全 0 的矩阵。这里 Mat6::zeros() 就是 6×6 全 0

        // 循环里填写：
        // 初始化状态向量 x_（位置 = 量测，速度 = 0）和协方差矩阵 P_（位置块 = R，速度对角 = vv，其余 0）
        for (int i = 0; i < 3; ++i)
        {
            x_[i] = p[i]; // 位置 = 量测位置（前 3 个）
            // 位置的协方差块（左上 3×3）= R（“位置的不确定度就是量测噪声”）
            for (int j = 0; j < 3; ++j)
                P_(i, j) = r(i, j); // Matx 的 operator() 是 (行, 列)
            // 速度的方差（对角线）= vv；速度本身还是 0（上面清零后没动过）
            P_(i + 3, i + 3) = vv;
        }
        // 初始化后：x_ = [p0, p1, p2, 0, 0, 0]，位置是量测值，速度是 0；P_ 左上是 R，速度对角是 vv，其余是 0
    }

    // 两个不同时刻才可观测速度；保留p-v交叉项，并补连续白加速度的端点速度误差。
    void CvFilter::bootstrap(const cv::Vec3d &p0, const cv::Matx33d &r0, const cv::Vec3d &p1,
                             const cv::Matx33d &r1, double dt, double q, double bound,
                             double position_bound)
    {
        // 验证输入
        // 验 p0、p1 （两个量测位置，速度是 (p1 - p0) / dt 算出来的）有限，r0、r1 （两个量测的噪声 R,必须正定，后面 P_ 的速度块要算 (r0 + r1) / dt²，R 不是合法协方差，P_ 就不是合法协方差）正定，dt （两帧时间间隔）有限且 > 0，bound 有限且 0 ≤ bound（时间不确定度界） < dt，position_bound（位置不确定度界,“时间不准导致的位置偏差”） 有限且 ≥ 0
        // bound = 两帧之和：time_bound(上一帧) + time_bound(当前帧);速度 v = (p1 - p0) / dt 依赖两个时刻，所以速度的不确定度要算两帧的时间不确定度;bound 还用来算最小可能 dt（dt - bound）
        // position_bound = 只当前帧：time_bound(当前帧);位置 x_ = p1 只来自当前帧的量测，所以位置的不确定度只跟当前帧的时间不确定度有关;时间不准 position_bound 秒，目标速度是 v，位置就可能偏掉 |v| × position_bound 米——所以它乘上速度就成了位置的界;bootstrap 的有界分支里 P_(i,j) 那项 position_bound² × (v[i]·v[j] + ...) 就来自这里（position_bound² × v²：“当前帧时间偏了 position_bound 秒，位置跟着偏 |v| × position_bound 米”的方差）
        // 速度看两帧，位置只看当前帧；离线均为0；time_uncertainty_s_ 由板子相机驱动填
        // 有一项不过就抛 TRACK_BAD_BOOTSTRAP
        if (!finite_vector(p0) || !finite_vector(p1) || !positive_definite(cv::Mat(r0)) ||
            !positive_definite(cv::Mat(r1)) || !std::isfinite(dt) || dt <= 0 ||
            !std::isfinite(bound) || bound < 0 || bound >= dt || !std::isfinite(position_bound) ||
            position_bound < 0)
            throw std::runtime_error("TRACK_BAD_BOOTSTRAP");

        // 算速度:v = (p1 - p0) / dt
        // 两个不同时刻的位置差除以时间间隔，就是速度——速度只有两个时刻才能观测出来
        // cv::Vec 的算术运算符：
        // operator-：Vec<T, cn> operator-(const Vec<T, cn>& a, const Vec<T, cn>& b)，逐分量相减
        // operator/：Vec<T, cn> operator/(const Vec<T, cn>& a, T scalar)，逐分量除以一个标量
        const auto v = (p1 - p0) / dt; // 两个时刻的位置差除以时间间隔，就是这段时间的平均速度
        // 验 v 有限，否则抛 TRACK_BOOTSTRAP_VELOCITY_OVERFLOW
        if (!finite_vector(v)) // 如果除法算出 NaN/∞（比如 dt 极小、位置差极大导致溢出），抛 TRACK_BOOTSTRAP_VELOCITY_OVERFLOW
            throw std::runtime_error("TRACK_BOOTSTRAP_VELOCITY_OVERFLOW");

        // 想删掉了，毫无道理，防御性保守放大P_
        // 有界分支(bound > 0):时间有不确定度时走的分支（离线恒为 0）
        // 用最小可能 dt（dt - bound）限制除法误差，算 velocity_mse，再填 x_ 和 P_，最后 checked_covariance 验 P_，返回
        if (bound > 0)
        {
            // 时间绝对界不是sigma。用最小可能dt限制除法误差，再用块MSE的2倍包络处理未知相关。
            // 时间戳不准 bound 秒，真实 dt 可能小到 dt - bound；速度是位置差除以 dt，dt 越小速度越大、速度的不确定度也越大，所以用最小 dt 来算最坏情况
            const double lower = dt - bound; // 最小可能 dt
            // 速度的均方误差，3×3 矩阵
            // 三项加起来，外面再乘个 2 包络未知相关
            // cv::Matx33d::eye()：Matx::eye()，静态方法，返回单位阵（对角线 1，其余 0）；跟 zeros()、ones() 是一家；这里 eye() * (q*(dt+bound)/3) 就是把标量 q*(dt+bound)/3 放到 3×3 对角线上（三个方向的速度方差都一样）
            // v * v.t()：外积。v 是 Vec3d（3×1 列向量），v.t() 是它的转置（1×3 行向量），两者相乘得 3×3 矩阵，第 (i,j) 项是 v[i]·v[j]；速度向量的外积，描述速度不确定度的方向结构
            const auto velocity_mse = 2 * (2 * (r0 + r1) / (lower * lower) + // 位置量测噪声（r0、r1）传到速度上的误差（位置差除以 dt，噪声也除以 dt，方差除以 dt²）
                                           (v * v.t()) * (bound * bound / (lower * lower))) + // 时间不确定度造成的速度误差（时间偏 bound，速度就偏 v·bound/lower）
                                      cv::Matx33d::eye() * (q * (dt + bound) / 3); // 加速度过程噪声在时间段里的速度方差（用 dt+bound 这个最大时间，往保守算）

            // 填 x_ 和 P_：位置 = p1，速度 = v
            P_ = Mat6::zeros();
            for (int i = 0; i < 3; ++i)
            {
                x_[i] = p1[i];
                x_[i + 3] = v[i];
                for (int j = 0; j < 3; ++j)
                {
                    // 有界初始化把观测时偏产生的位置位移计入位置协方差。
                    // 单帧界来自时间戳；用两次观测形成的速度二阶矩计算位移不确定性。
                    // P_ 位置块 = 2·(r1 + position_bound²·(v·vᵀ + velocity_mse))——r1 是第二帧量测噪声，position_bound² 那项是当前帧时间偏导致的位置位移（position_bound × v 是位移，平方是方差）
                    P_(i, j) = 2 * (r1(i, j) + position_bound * position_bound *
                                                   (v[i] * v[j] + velocity_mse(i, j)));
                    // 速度块 = 2·velocity_mse
                    P_(i + 3, j + 3) = 2 * velocity_mse(i, j);
                    // 注意这里没有位置–速度交叉项（常规分支有），有界分支不精细建模交叉相关，直接把对角块放大 2 倍包络过去
                }
            }
            // checked_covariance + return：验 P_ 是合法协方差（有限、对称、正定），然后直接返回，不走常规分支
            P_ = checked_covariance(cv::Mat(P_));
            return;
        }

        // 常规分支(bound == 0):离线走的分支
        // 标准处理：状态 x 由两个量测线性定义，P_ 就是把量测噪声（r0、r1）按线性变换传过去（P = A·R·Aᵀ 的展开），再补一个过程噪声项
        // x_ = [p1, v]，P_ 位置块 = r1，位置–速度交叉项 = r1/dt，速度块 = (r0+r1)/dt²，速度对角再加 q·dt/3，最后 checked_covariance 验 P_
        P_ = Mat6::zeros();
        for (int i = 0; i < 3; ++i)
        {
            // 填x_：位置 = p1，速度 = v
            x_[i] = p1[i];
            x_[i + 3] = v[i];
            // 填P_：误差传递（定初始的P）
            // ！！方差组合公式：Var(aX + bY) = a²Var(X) + b²Var(Y) + 2ab·Cov(X, Y) ； Cov(X, aY + bZ) = a·Cov(X,Y) + b·Cov(X,Z)
            for (int j = 0; j < 3; ++j)
            {
                // 位置块 = r1：位置就是 p1，不确定度就是 R1
                P_(i, j) = r1(i, j);
                // 交叉项 = r1/dt：位置和速度都沾 p1，p1 偏 δ，位置偏 δ、速度偏 δ/dt；Cov(eₚ, eᵥ) = Cov(ε₁, (ε₁ − ε₀)/dt)= (1/dt)·Cov(ε₁, ε₁) + (−1/dt)·Cov(ε₁, ε₀)= (1/dt)·Var(ε₁) + 0（独立）= r₁/dt
                // 位置和速度的误差是连着的；两个一起偏，方向还一致；它们不是独立的，是正相关，协方差就是 r1/dt
                P_(i, j + 3) = P_(j + 3, i) = r1(i, j) / dt;
                // 速度块 = (r0+r1)/dt²：速度是两个位置的差除以 dt，两个位置的噪声都传进来；速度 v = (p1−p0)/dt 是两个带噪声的位置算出来的，p0、p1 的误差独立，方差相加
                P_(i + 3, j + 3) = (r0(i, j) + r1(i, j)) / (dt * dt);
            }
            // “过程噪声对初始 P 的贡献”；三个轴的过程噪声相互独立
            // x_ 里的速度 v = (p1−p0)/dt 是区间平均速度，但滤波器在 t1 时刻的状态应该表示t1 时刻的瞬时速度 v₁
            // 设 t0 时刻真速度是 v₀，区间里有个随机加速度 a(t)（白噪声，PSD（噪声强度） 是 q）：
            //   t1 时刻真速度：v₁ = v₀ + ∫₀^dt a(t)dt（加速度直接累加到速度上，每个时刻的加速度权重都是 1）
            //   平均速度：v_avg = (1/dt)·∫₀^dt v(t)dt = v₀ + (1/dt)·∫₀^dt (dt−t)·a(t)dt 二重积分，交换次序（加速度要先影响位置再除以 dt，早的加速度有更长时间去影响位置，所以权重 (dt−t)/dt 从 1 递减到 0）
            // dt 里随机加速度造成x_ 里速度的误差：e = v_avg − v₁ = ∫₀^dt [(dt−t)/dt − 1]·a(t)dt = −(1/dt)·∫₀^dt t·a(t)dt
            //    权重是 −t/dt：t=0 时是 0，t=dt 时是 −1；晚的加速度对这个误差贡献最大——因为平均速度几乎不算晚的加速度（权重接近 0），而端点速度全算（权重 1）
            // 白噪声（每时每刻的值都是随机的，时间上不相关）性质 E[a(s)a(u)] = q·δ(s−u)，只有 s=u 时才有贡献：Var(e) = (1/dt²)·E[(∫₀^dt t·a(t)dt)²] = (1/dt²)·∫₀^dt∫₀^dt t·u·E[a(t)a(u)] dtdu = (1/dt²)·q·∫₀^dt∫₀^dt t·u·δ(t−u) dtdu = (1/dt²)·q·∫₀^dt t² dt = q·dt/3
            // 速度对角再加 q·dt/3：这 dt 里目标可能有随机加速度，平均速度跟端点瞬时速度有偏差，补过程噪声的贡献（1/3 是连续白噪声加速度模型积分出来的）
            // q·dt 是端点速度（v₁ = v₀ + ∫₀^dt a(t)dt）的过程噪声（∫₀^dt a(t)dt）方差（权重全是 1，∫₀^dt 1² dt = dt，Var(∫₀^dt a(t)dt) = E[(∫₀^dt a(t)dt)²] = ∫₀^dt∫₀^dt E[a(t)a(u)] dtdu = q·∫₀^dt∫₀^dt δ(t−u) dtdu = q·∫₀^dt 1 dt = q·dt）；平均速度的权重是三角形的（从 1 递减到 0），平方积分出来只有 dt/3，所以是 q·dt/3，比端点速度小
            // 这里补的是“平均速度跟端点速度的差”的方差，所以是 q·dt/3
            // ！！！重要区分说明：bootstrap 的 q·dt/3 是“平均速度≠端点速度”的代价；predict 的 q·dt 是“速度自己随时间乱跑”的量；速度的定义不同（平均 vs 瞬时），方差自然不同
            // bootstrap（第一次）是在“造”初始状态：速度是拿两个位置算出来的平均速度 (p1−p0)/dt；但滤波器在 t1 时刻的状态应该表示瞬时速度 v₁。平均速度跟瞬时速度差一个量——随机加速度在这 dt 里的作用，那个差的方差就是 q·dt/3。所以 bootstrap 的 q·dt/3 是“拿平均速度冒充瞬时速度”的误差，是估计误差
            // predict（后面）是在“推”已有状态：状态里已经有个瞬时速度 v，往前推 dt，真实速度会随机变，变了多少是 ∫₀^dt a(t)dt，方差是 q·dt。所以 predict 的 Q 里速度块是 q·dt，它是真实状态随时间随机演化的量，不是估计误差。
            // bootstrap 造完之后，滤波器就把那个平均速度当成瞬时速度来用了，q·dt/3 就是为这次“冒充”付的误差
            P_(i + 3, i + 3) += q * dt / 3;
        }
        // 验P_并微调
        P_ = checked_covariance(cv::Mat(P_));
    }

    // 预测：匀速模型，位置 = 位置 + 速度·dt，速度 = 速度；协方差按线性传播 + 过程噪声 Q；Q 是连续白加速度模型积分出来的 dt³、dt²、dt 项
    // 匀速状态模型：状态 x = [位置 p, 速度 v]（6 维），假设速度恒定，
    // 位置按 p += v·dt 递推，速度不变。偏离匀速的部分用白噪声加速度建模，
    // 强度为 q（功率谱密度，m²/s³），过程噪声 Q 由 q 和 dt 推出：
    // 位置块 q·dt³/3、交叉项 q·dt²/2、速度块 q·dt。
    // ！！！状态方程：x_k = F·x_{k-1} + w_{k-1}，F = [I, dt·I; 0, I]，w 的协方差为 Q；E[w] = 0，Q = E[w·wᵀ]，wₖ₋₁：过程噪声——这 dt 里随机加速度对状态的扰动
    // 实际dt的连续白加速度CV模型；q单位m²/s³，不与离散常加速度dt⁴模型混用。（这里是连续白噪声模型，指数是 3、2、1）
    void CvFilter::predict(double dt, double q, double bound)
    {
        // 验输入
        // dt 和 bound 都得有限、≥ 0；如果 bound > 0 还要求 dt > bound（不然 dt − bound 没意义）
        // 不满足就抛错
        if (!std::isfinite(dt) || dt < 0 || !std::isfinite(bound) || bound < 0 ||
            (bound > 0 && dt <= bound))
            throw std::runtime_error("TRACK_BAD_PREDICTION_INTERVAL");

        // 造 F 和 Q
        // F：匀速模型的状态转移矩阵。eye() 先给对角线 1，再把 f(i,i+3) = dt——新位置 = 旧位置 + 速度×dt，速度不变。就是 x_new = [p + v·dt, v]
        // F——状态转移矩阵：模型规定的“状态怎么变”；状态是 x = [位置 p，速度 v]；匀速模型：新位置 = 旧位置 + 旧速度 × dt；新速度 = 旧速度（不变）
        // 写成矩阵乘法 x_new = F·x，F 就是：
        /*
        F = [ I   dt·I ]     I 是 3×3 单位阵
            [ 0   I    ]
        代码里 Mat6::eye() 先给对角线全 1（位置→位置、速度→速度都是 1），再 f(i,i+3) = dt 把右上 3×3 块填成 dt（“位置 += 速度×dt”）；F 里其他地方都是 0
        F 是确定性的，没有随机性，纯粹是匀速模型的运动学
        用处：推状态：x_ = f * x_，新位置 = 旧位置 + 速度×dt；推协方差：propagated = f * P_ * f.t() + noise，f * P_ * f.t()（F·P·Fᵀ），意思是“旧的不确定度按 F 搬到新时刻”（位置的不确定度会因为速度的不确定度而长大（速度不准，dt 时间后位置就更不准））
        */
        // Q（叫 noise）：连续白噪声加速度的过程噪声，三个块分别是 q·dt³/3、q·dt²/2、q·dt；这里用的是（删） upper = dt + bound（时间不准时往大了算）；bound = 0 时 upper = dt，就是标准 Q
        // Q——过程噪声：模型之外的“随机乱动”；Q（代码里叫 noise）是这 dt 里真实状态自己随机变了多少，由白噪声加速度模型定；三个块都是 q 和 dt 积分出来的
        /*
        速度块 q·dt：速度变化 Δv = ∫₀^dt a(t)dt，权重全是 1，Var = q·∫₀^dt 1 dt = q·dt
        位置块 q·dt³/3：位置变化 Δp = ∫₀^dt (dt−t)·a(t)dt，权重是 (dt−t)（早的加速度有更长时间影响位置），Var = q·∫₀^dt (dt−t)² dt = q·dt³/3
        交叉项 q·dt²/2：位置变化和速度变化的协方差，Cov = q·∫₀^dt (dt−t) dt = q·dt²/2；位置和速度的变化都沾同一个加速度，所以相关
        Q = q·[[dt³/3, dt²/2],[dt²/2, dt]]（每块再乘 3×3 单位阵）
        Q = ┌ q·dt³/3·I₃    q·dt²/2·I₃ ┐
            └ q·dt²/2·I₃    q·dt·I₃    ┘
        四个3×3 块（位置块，速度块，速度-位置交叉，速度-位置交叉），每个 3×3 块都是对角阵（三个轴独立，互不相关），非对角线全是 0
        */
       // 合起来使用：P_new = F·P·Fᵀ + Q
       // F·P·Fᵀ：旧的不确定度，按运动模型搬到新时刻（速度的不确定度会“漏”进位置里）
       // + Q：再新加一段不确定度——这 dt 里目标自己随机加速了，哪怕之前完全确定，现在也不确定了
       // F （旧账）是“确定性的搬运”，Q（新账） 是“新加的随机性”
        Mat6 f = Mat6::eye(), noise = Mat6::zeros();
        const double upper = dt + bound;
        for (int i = 0; i < 3; ++i)
        {
            f(i, i + 3) = dt;
            noise(i, i) = q * upper * upper * upper / 3;
            noise(i, i + 3) = noise(i + 3, i) = q * upper * upper / 2;
            noise(i + 3, i + 3) = q * upper;
        }

        // 推 P_
        // 解析：F·P·Fᵀ “旧的不确定度按运动模型搬到新时刻”，速度的不确定度会漏进位置里
        // 状态按 F 变：x_new = F·x_old。均值也按 F 变：x̄_new = F·x̄_old。误差是状态减均值：e_new = x_new − x̄_new = F·x_old − F·x̄_old = F·(x_old − x̄_old) = F·e_old；误差向量跟状态走一样的变换
        // 协方差是误差的外积：P = E[e·eᵀ]。把 e_new = F·e_old 代进去：P_new = E[e_new·e_newᵀ] = E[(F·e_old)·(F·e_old)ᵀ]，(F·e)ᵀ = eᵀ·Fᵀ，所以：P_new = E[F·e_old·e_oldᵀ·Fᵀ] = F·E[e_old·e_oldᵀ]·Fᵀ = F·P_old·Fᵀ（F 是确定的数，可以提出期望外面；左边一个 F（管 e），右边一个 Fᵀ（管 eᵀ））
        // 标准的卡尔曼预测：P_new = F·P·Fᵀ + Q。F 把旧的不确定度搬到新时刻，Q 再加上这段时间的随机加速度
        // Matx 的 operator*：Matx::operator*，矩阵乘法（按维度走）；f * P_ * f.t() 是 6×6 矩阵连乘（先 f*P_ 得 6×6，再乘 f.t()），f * x_ 是 6×6 矩阵乘 6 维向量得 6 维向量
        Mat6 propagated = f * P_ * f.t() + noise;
        // Young 不等式那套时间不确定度处理（控制未知时偏和状态误差的相关项），类似bootstrap 的有界分支
        if (bound > 0)
        {
            // Young不等式控制未知时偏与状态误差的相关项；bound/dt由量纲给出，不是经验调参。
            Mat6 a = Mat6::zeros();
            for (int i = 0; i < 3; ++i)
                a(i, i + 3) = 1;
            const double ratio = bound / dt;
            const auto second_moment_bound = 2 * (P_ + x_ * x_.t());
            propagated = (1 + ratio) * (f * P_ * f.t()) + noise +
                         (1 + 1 / ratio) * bound * bound * (a * second_moment_bound * a.t());
        }

        // 推 x_、验、提交 P_
        // x_ = f * x_：状态预测，新位置 = 旧位置 + 速度×dt，速度不变
        x_ = f * x_;
        // 遍历 x_.val（6 个分量）验有限，溢出就抛 TRACK_PREDICTION_STATE_OVERFLOW
        // Vec::val：cv::Vec 底下存数据的 C 数组（_Tp val[cn]）；x_ 是 Vec6，x_.val 就是 6 个 double 的数组。for (double value : x_.val)是范围 for，直接遍历这 6 个数。
        for (double value : x_.val)
            if (!std::isfinite(value))
                throw std::runtime_error("TRACK_PREDICTION_STATE_OVERFLOW");

        // P_ = checked_covariance(...)：验 propagated 合法（对称、正定）再存回去
        P_ = checked_covariance(cv::Mat(propagated));
    }

    // innovation：只处理位置（3 维），速度不直接出现。
    //   残差 innovation = z - position()：量测减预测位置，z 是这帧 G2 给的位置量测。
    //   S = position_block(P-) + R：残差的方差。position_block(P-) 是预测位置方差
    //     （H = [I3 0]，H·P-·H^T 正好是 P- 左上 3x3 块）；R 是量测噪声方差。
    //     残差 = v - e（量测噪声减预测误差），两者独立，方差相加。
    //   NIS = innovation^T · S^-1 · innovation，中文：归一化新息平方。
    //     不是只比 S：S 是“允许偏多少”，残差是“实际偏了多少”，NIS 是“偏了几个 sigma”。
    //     S 小但残差大 = 坏量测，比 S 会挑错；NIS 同时看分子分母才挑得对。
    //   严格推导（白化）：S = L·L^T（Cholesky），y = L^-1·innovation ~ N(0, I)，
    //     NIS = y1^2 + y2^2 + y3^2 ~ χ²3，99% 阈值 11.34。
    //   设计决定：NIS 只排序、不设拒绝门。多个候选量测比 NIS 挑最小的吃；
    //     只有一个量测时直接吃，再大也不拒。常规做法是超阈值拒掉，我们刻意不设。
    //   速度怎么被修正：correct 里 K = P·H^T·S^-1，P 的位置–速度交叉项把位置残差传到速度上。

    // innovation 本身只处理位置；innovation 只管评估位置量测，correct 才把这份评估扩到整个状态
    // 残差是位置差（实际的偏差），S 是位置方差（不确定度），NIS 是位置量测的可信度分数（归一化新息（残差的学名）平方，残差按它自己的不确定度（S=σ·σ）归一化，算出来是“偏了几个 σ”）
    // “量测评估”，夹在 predict 和 correct 中间（predict 推出预测，innovation 评估量测，correct 拿评估结果去更新状态）
    // 算残差：量测跟预测差多少（z − 预测位置），这是更新的原材料；算 S：残差的不确定度（预测位置方差 + 量测噪声 R），correct 算卡尔曼增益要用它；
    // 算 NIS：残差按 S 归一化后的分数（马氏距离平方），衡量这帧量测跟预测“合不合得上”；advance 的 update 块拿它给多个候选量测排序，挑最一致的那个
    // 用 Cholesky 解线性方程，不显式求逆；NIS 只用于量测关联排序，不设拒绝门槛。
    // 输入：z 就是这帧 检测 给的位置量测（t_m_），r 是配套的 R（profile 的 camera_mse_m2_ + S）；
    // 流程：检测输出 PoseEstimate → measurement_adapter 把它包成 measurement{t_m_, R} → advance 的 update 块里拿 measurements[i].position_ 和 measurements[i].R_ 调 filter_.innovation(...)
    InnovationResult CvFilter::innovation(const cv::Vec3d &z, const cv::Matx33d &r) const
    {
        // 验输入
        // 量测 z 得有限，R 得正定，不然抛错
        if (!finite_vector(z) || !positive_definite(cv::Mat(r)))
            throw std::runtime_error("TRACK_BAD_MEASUREMENT_NOISE");

        // 算残差 innovation
        // 残差 = 实际量测 − 预测位置；position() 是 x_ 的前 3 个分量（预测的位置）
        // 残差告诉你这帧量测跟预测差多少，是“意外程度”
        const auto innovation = z - position();

        // 算 S
        // S 是残差的协方差：预测位置的不确定度 + 量测噪声
        // ！！原理：残差是两个误差的差。把量测和预测都写成“真值 + 误差”：量测 z = 真值 + v，v 是量测噪声，方差是 R；预测 position() = 真值 + e，e 是预测误差，方差是 position_block(P_)
        //       残差 = z − position() = v − e。v 和 e 独立（量测噪声跟预测误差没关系），差的方差等于方差之和：Var(残差) = Var(v) + Var(e) = R + position_block(P_)
        // position_block(P_) 取 P_ 左上 3×3（位置块），因为 H = [I₃ 0₃] 只量位置，所以 H·P⁻·Hᵀ 就是 P_ 的位置块；再加 r（R）就是 S
        // 公式就是 S = H·P⁻·Hᵀ + R；
        // H = [I₃ 0₃]（3×6，左边单位阵、右边 0），P⁻ 是 6×6：H·P⁻：H 左乘 P⁻，挑出 P⁻ 的上 3 行（位置块 + 位置–速度交叉块）；再右乘 Hᵀ：挑出左 3 列，剩下左上 3×3
        // P⁻ 读作“P minus”：predict 算出来的协方差（先验），P⁻ = F·P·Fᵀ + Q（预测值，还没吃进量测）；P（没上标）：correct 更新后的协方差（后验），P = (I − K·H)·P⁻
        // 预测/更新
        const auto s = position_block(P_) + r;

        // 算 NIS：NIS = innovationᵀ·S⁻¹·innovation（马氏距离的平方，，衡量残差偏离 0 有几个“标准差”（按 S 的尺度））
        // ！！重要规矩：线性变换 y = A·x，协方差就变成 A·Cov(x)·Aᵀ；Cov(y) = E[(y−ȳ)(y−ȳ)ᵀ]
        // 马氏距离：一种考虑了数据分布的统计距离（“在当前数据的分布形态下，这个点偏离中心有多远”）
        /* 原理：NIS 就是马氏距离的平方，来自高斯分布（高斯分布下“标准化距离”的唯一自然定义）的性质；高斯假设下 NIS 服从 χ² 分布（3 维就是 χ²₃），所以才能用 χ² 阈值（11.34）来判断一致性
        1. 残差是高斯的：卡尔曼假设下，残差服从零均值高斯分布，协方差就是 S：innovation ~ N(0, S)
        2. 一维的标准化距离：一维高斯 x ~ N(μ, σ²)，“偏了几个 σ”就是 (x−μ)/σ，平方是 (x−μ)²/σ²。分母 σ² 是方差，分子是偏差的平方
        3. 推广到多维：多维高斯里，S⁻¹ 就是 1/σ² 的矩阵版；NIS = innovationᵀ·S⁻¹·innovation，就是把一维的 (x−μ)²/σ² 推广到多维：
           - innovationᵀ 和 innovation 是偏差向量（两边各一个）；
           - 中间的 S⁻¹ 起“除以方差”的作用，同时处理各轴方差不同和轴间相关 
        4. 严格推导：
            残差是高斯的：innovation ~ N(0, S)，S对称正定
            白化：S 正定就有 Cholesky 分解 S = L·Lᵀ；定义白化后的变量：y = L⁻¹·innovation；y 的协方差是 L⁻¹·S·(L⁻¹)ᵀ = L⁻¹·L·Lᵀ·(L⁻¹)ᵀ = I，所以：y ~ N(0, I)，三个分量独立标准正态
            NIS 就是 y 的平方和：yᵀ·y = y₁² + y₂² + y₃²，把 y = L⁻¹·innovation 代回去：
                yᵀ·y = (L⁻¹·innovation)ᵀ·(L⁻¹·innovation)
                = innovationᵀ·(L⁻¹)ᵀ·L⁻¹·innovation
                = innovationᵀ·(L·Lᵀ)⁻¹·innovation
                = innovationᵀ·S⁻¹·innovation
            结论：NIS = y₁² + y₂² + y₃²，三个独立标准正态的平方和，所以 NIS ~ χ²₃；S⁻¹ 是白化变换的平方（把相关高斯变成独立标准高斯），NIS 是白化后向量的模长平方
            概念说明：
            1. Cholesky 分解:对称正定矩阵 S 可以写成 S = L·Lᵀ，其中 L 是下三角矩阵（对角线以上全 0）；相当于矩阵的“平方根”：正数 a = (√a)²，正定矩阵 S = L·Lᵀ；用处：解 S·x = b 时 Cholesky 比通用 LU 更快更稳；L 还是白化的工具
            2. 白化：把相关的高斯向量变成不相关的标准高斯向量（线性变换）；白化后的协方差是单位阵 I，三个分量独立标准正态；名字来自信号处理：白噪声各频率功率相等（平坦谱），相关信号是“有色”的，白化就是把它“漂白”成白的；解析：innovation ~ N(0, S) 是“有色”的：各轴方差不同、轴间相关。白化 y = L⁻¹·innovation 之后：y ~ N(0, I) 三个分量独立标准正态；几何上就是把 S 的不确定性椭球“捏”成单位球——各方向方差都是 1，相关性也没了
               NIS = yᵀ·y 就是白化后向量的模长平方，三个独立标准正态的平方和，所以服从 χ²₃。S⁻¹ 出现就是因为白化矩阵是 L⁻¹，(L⁻¹)ᵀ·L⁻¹ = S⁻¹
        */
        // 这里不显式求 S⁻¹，而是用 Cholesky 解线性方程 S·x = innovation，再算 innovation·x——数值更稳，也更快
        // solved 就是 S⁻¹·innovation；score 就是 NIS，模型正确时服从 χ²₃ 分布，99% 阈值 11.34
        cv::Mat solved;
        // cv::solve：bool cv::solve(InputArray src1, InputArray src2, OutputArray dst, int flags = DECOMP_LU)，解线性方程组 src1·dst = src2
        // cv::DECOMP_CHOLESKY 是 Cholesky 分解，只适用于对称正定矩阵（S 就是），比 LU 更快更稳（这里 S 对称正定，Cholesky 是最合适的；LU 也能解但没必要，SVD 太慢）
        // cv::solve 的 flags 是选分解方法：DECOMP_LU（默认）：通用 LU，任何可逆方阵都能解；DECOMP_CHOLESKY：只用于对称正定矩阵（S 就是），比 LU 快一倍、更稳；DECOMP_SVD：奇异值分解，什么矩阵都能解（包括奇异、非方阵），最稳但最慢；DECOMP_QR、DECOMP_EIG：其他分解
        // 返回 false 表示解失败
        if (!cv::solve(cv::Mat(s), cv::Mat(innovation), solved, cv::DECOMP_CHOLESKY))
            throw std::runtime_error("TRACK_NIS_SOLVE_FAILED");
        // Matx/Vec 和 Mat 之间要显式搭桥
        // s 是 Matx33d、innovation 是 Vec3d，都是定长类型；cv::solve 要的是 InputArray（实际按 Mat 处理），.dot() 是 Mat 的方法；包一层 cv::Mat(...) 就是把定长类型转成 Mat，好传给这些函数
        // Mat::dot：double Mat::dot(InputArray m) const，两个 Mat 的点积（逐元素相乘再求和）
        // 这里 cv::Mat(innovation).dot(solved) 就是 innovationᵀ·solved，算出 NIS
        // 两个列向量的点积就是 aᵀ·b（逐元素相乘再求和），所以 cv::Mat(innovation).dot(solved) 正好就是 innovationᵀ·solved，也就是 NIS
        // 点积是算内积的现成工具（.t() 也能写）
        const double score = cv::Mat(innovation).dot(solved);

        // 验 score、返回
        // NIS 必须是有限的非负数（理论上 innovationᵀ·S⁻¹·innovation ≥ 0，因为 S 正定）
        if (!std::isfinite(score) || score < 0)
            throw std::runtime_error("TRACK_INVALID_NIS");
        // 返回 InnovationResult{残差， S， R， NIS}
        return {innovation, s, r, score};
    }

    // correct 是卡尔曼更新步，拿 innovation 算好的残差、S、R，算出卡尔曼增益 K = P·Hᵀ·S⁻¹，更新状态 x = x⁻ + K·残差，再用 Joseph 形式更新协方差 P
    void CvFilter::correct(const InnovationResult &innovation)
    {
        // 造 H
        // H = [I₃ 0₃]（3×6，左边单位阵、右边 0），只量位置
        // zeros 先造 3×6 全 0，再把对角线前 3 个置 1
        const auto &r = innovation.R_;
        cv::Mat h = cv::Mat::zeros(3, 6, CV_64F); // Mat::zeros：static Mat Mat::zeros(int rows, int cols, int type)，造全 0 的 Mat；这里 zeros(3, 6, CV_64F) 是 3×6 的 double 全 0
        for (int i = 0; i < 3; ++i)
            h.at<double>(i, i) = 1; // Mat::at（模板方法，需写明元素类型）：template<typename _Tp> _Tp& at(int i0, int i1); 按类型访问 (i0, i1) 处的元素，返回引用，可读可写；at<double> 就是告诉编译器：把 (i, i) 这个位置按 double 解释，返回一个 double&，这样才能赋值

        // 算卡尔曼增益 K：K = P·Hᵀ·S⁻¹
        // 这里不显式求 S⁻¹：pht = P·Hᵀ（6×3），solve(S, pht.t(), solved) 解 S·X = H·P 得 solved = S⁻¹·H·P，再转置 gain = solved.t() = P·Hᵀ·S⁻¹
        // Cholesky 解，失败抛 TRACK_GAIN_SOLVE_FAILED
        // 把Matx包成Mat构造：template<typename _Tp, int m, int n> Mat(const Matx<_Tp, m, n>& m, bool copyData = false); 默认 copyData = false，所以 不拷贝，只包一层 Mat 头、共享 P_ 的存储
        cv::Mat p(P_), solved;
        const cv::Mat s(innovation.S_);
        const cv::Mat pht = p * h.t();
        // 不显式求逆，用 solve 解线性方程组代替；这里 S 是 3×3，P·Hᵀ 是 6×3，solve(S, ...) 解的是 S·X = B，所以不能直接解 S·X = P·Hᵀ（行数对不上：S 是 3 行，P·Hᵀ 是 6 行）
        // 利用转置技巧：解 S·X = (P·Hᵀ)ᵀ = H·P 得到 X = S⁻¹·H·P = Kᵀ，再转置 gain = Xᵀ = P·Hᵀ·S⁻¹；因为 cv::solve 只会左除（解 A·X = B），想算右除（X·S = P·Hᵀ）就得先转置、解、再转回来
        if (!cv::solve(s, pht.t(), solved, cv::DECOMP_CHOLESKY))
            throw std::runtime_error("TRACK_GAIN_SOLVE_FAILED");
        const cv::Mat gain = solved.t();

        // 更新状态 x：x = x⁻ + K·残差
        // 这里x_ 是预测的 x⁻：predict 先跑完，x_ 已经被更新成预测值 x⁻ = F·x，P_ 也成了 P⁻；innovation 是 const 方法，不改 x_，它用 position()（读的 x_ 的位置部分，也就是 x⁻ 的位置）算残差 z − H·x⁻；correct 里的 cv::Mat(x_) 读到的就是 x⁻，state = x⁻ + K·残差 算出来的是更新后的 x̂，最后 copyTo 把 x̂ 写回 x_
        // checkRange 验有限，溢出抛错；copyTo 写回 x_（false 是关键）
        const cv::Mat state = cv::Mat(x_) + gain * cv::Mat(innovation.residual_);
        if (!cv::checkRange(state))
            throw std::runtime_error("TRACK_CORRECTION_STATE_OVERFLOW");
        state.copyTo(cv::Mat(x_, false));

        // 更新协方差 P（Joseph 形式）：
        // a = I − K·H；P = a·P⁻·aᵀ + K·R·Kᵀ，这是 Joseph 形式；数学上等于标准形式 (I−K·H)·P⁻，但数值更稳：两项都是对称的，和一定对称正定，不会因为舍入误差丢对称性
        // 标准形式：P = (I − K·H)·P⁻，数学上等价，但数值不稳：舍入误差可能让 P 失去对称性、正定性；Joseph 形式：P = (I − K·H)·P⁻·(I − K·H)ᵀ + K·R·Kᵀ，数学上等价，但数值更稳（两项对称半正定矩阵的和，和一定对称正定）：两项都是对称的，和一定对称正定，不会因为舍入误差丢对称性
        // checked_covariance 再验一遍才存
        const cv::Mat a = cv::Mat::eye(6, 6, CV_64F) - gain * h; // Mat::eye：static Mat Mat::eye(int rows, int cols, int type)，造单位阵；这里 eye(6, 6, CV_64F) 是 6×6 的 double 单位阵
        P_ = checked_covariance(a * p * a.t() + gain * cv::Mat(r) * gain.t());
    }

    // CvFilter 的方法，返回 x_ 的前 3 个分量（预测位置），cv::Vec3d
    cv::Vec3d CvFilter::position() const
    {
        return {x_[0], x_[1], x_[2]};
    }

    // CvFilter 的方法，返回 x_ 的后 3 个分量（预测速度），cv::Vec3d
    cv::Vec3d CvFilter::velocity() const
    {
        return {x_[3], x_[4], x_[5]};
    }

} // namespace mark::tracking::detail
