// 沿原图角片轮廓提取两组连续边支持，各拟合一次直线并以交点作为精修角。
#include "corners/corner_edge_fit.hpp"
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <limits>

namespace mark::detector
{
    // 沿着原图轮廓，从粗角点出发往两边走，各取一段属于一条边的点，分别拟合出一条直线，两条直线的交点就是精修后的角点
    std::optional<cv::Point2d> fit_observed_corner(const std::vector<cv::Point> &contour_,
                                                   cv::Point2d corner,
                                                   const std::array<cv::Point2d, 2> &adjacent,
                                                   std::string &reason_)
    {
        // 开头（fail + 校验）：
        // 先定义一个 fail 小工具（填原因、返回空），清掉 reason_
        // 一个 lambda（写在原地、不起名字的函数（“只用一次、又想用周围变量的小函数”）），把传进来的原因字符串写进 reason_，再返回一个空 optional
        // [&] 是按引用捕获（这样它才能改外面的 reason_）；
        // (const char *s) 是参数（原因字符串）；
        // -> std::optional<cv::Point2d> 是尾置返回类型（跟函数返回类型一样）
        // 后续每个失败点都是一行 return fail("原因");，不用每次写两行
        auto fail = [&](const char *s) -> std::optional<cv::Point2d>
        {
            reason_ = s;
            return {};
        };
        reason_.clear(); // 先清原因
        // 然后校验轮廓点数和角点有限性（输入校验）
        if (contour_.size() < 2 || !std::isfinite(corner.x) || !std::isfinite(corner.y))
            return fail("MISSING_CONTOUR"); // “没法拟合”
        
        // 方向归一化：
        // 算出从粗角指向两个邻角的两个方向向量（两条邻边），归一化成单位向量    
        std::array<cv::Point2d, 2> directions{adjacent[0] - corner, adjacent[1] - corner};
        for (auto &v : directions)
        {
            double n = cv::norm(v);
            if (!std::isfinite(n) || n == 0)  
                return fail("INVALID_GUIDANCE"); // 长度不是有限数，或者长度是 0（邻角和这个角重合了，方向不存在），就失败，原因"INVALID_GUIDANCE"（两个方向是后面拟合的“指导”，指导本身坏了就没法往下走）
            v *= 1 / n; // 归一化成单位向量（让后续点积只反映夹角）
        }

        // 找锚点：
        // 在轮廓上找离粗角最近的点，当作沿轮廓走的起点（锚点 anchor）
        size_t anchor = 0;
        for (size_t i = 1; i < contour_.size(); ++i)
            if (cv::norm(cv::Point2d(contour_[i]) - corner) <
                cv::norm(cv::Point2d(contour_[anchor]) - corner))
                anchor = i; // 这个点是后面沿轮廓往两边走的起点——粗角是近似位置，轮廓上离它最近的点才是真正的角附近

        // 小工具 + 常数：
        // point(k)（带绕回的取点）、tangent_window=3、trim=2、probe 这些
        const int count = int(contour_.size()); // 轮廓点数，转成 int（后面下标运算用 int，方便处理负数）
        // 取轮廓上第 k 个点的 lambda
        // (k % count + count) % count是绕回逻辑（C++ 里负数取余还是负数，加一个 count 再取余就把它规范到 [0, count)）
        // 轮廓是个闭环，往前走过头从末尾绕回来，往后走过头从开头绕回来
        auto point = [&](int k)
        {
            return cv::Point2d(contour_[(k % count + count) % count]);
        };
        // 3像素半窗平滑CHAIN_APPROX_NONE的栅格台阶，2像素trim去除转折混合。
        // 都只定义支持提取，不是拟合后的质量拒绝预算；不按帧/设备调路线。
        // 起步跳过（2 + 3 = 5）和末尾裁掉（2）的量（像素级固定值（像素噪声的尺度是固定的））;constexpr 是“编译期常量”
        constexpr int tangent_window = 3; // 算切线时的半窗，3 像素（逐像素轮廓是阶梯状的，切线要跨 3 像素取才平滑）
        constexpr double trim = 2; // 2 像素的裁剪（角附近轮廓从一条边转到另一条边，转折处 2 像素内的点是两条边的混合，不要）
        auto bounds = cv::boundingRect(contour_);
        const int probe = std::max(3, std::max(bounds.width, bounds.height) / 4); // 探测距离（按轮廓大小自适应的（MARK 可近可远）），取 max(3, 宽高较大者/4)——至少 3 像素，否则是轮廓包围盒较大边的四分之一；后面用它从锚点往两边各探一步，判断这条边属于哪一组
        // Vec4d 是 4 个 double 定长向量（Vec<double, 4>）
        // 下标访问：lines[side][0]、[1] 是方向 (vx, vy)，[2]、[3] 是线上一点 (x0, y0)
        cv::Vec4d lines[2]; // 装两条拟合直线（cv::Vec4d 是 fitLine 的输出格式：方向 + 线上一点）
        int previous_group = -1; // 记上一条边的分组（初始 -1 是个无效值，保证第一条边一定能通过“两边分组不同”的检查）

        // 逐边循环：两条边各走一遍——定方向、沿轮廓收集属于这条边的点、拟合直线
        // 两条边各走一遍——从锚点沿轮廓往一个方向走，收集属于这条边的点，拟合成一条直线
        for (int side = 0; side < 2; ++side)
        {
            // 定方向
            int sign = side ? 1 : -1; // side=0 往轮廓下标减的方向走，side=1 往加的方向走——两边各走一个方向
            const auto motion = point(int(anchor) + sign * probe) - point(int(anchor)); // 从锚点往这个方向探 probe 步，得到位移向量
            int group = motion.dot(directions[1]) > motion.dot(directions[0]); // 这个位移跟哪个指导方向更贴近（点积大的那个），就是这条边属于第 0 组还是第 1 组；> 返回 bool，存进 int 就是 0 或 1
            if (group == previous_group)
                return fail("NO_TWO_DISTINCT_EDGES"); // 两条边必须分属不同组——如果这边的组跟上边一样，说明两边走的是同一条边，失败 "NO_TWO_DISTINCT_EDGES"
            previous_group = group;

            // 沿轮廓走、收集点
            std::vector<cv::Point2d> support;
            bool started = false;
            double walk = 0; // 从锚点出发沿轮廓走过的距离
            auto last = point(int(anchor)); // last 是上一个点
            for (int step = 1; step < count / 2; ++step)
            {
                int k = int(anchor) + sign * step;
                auto p = point(k);
                walk += cv::norm(p - last);
                last = p;
                // 起步的前 trim + tangent_window 像素跳过（头 trim 像素是转折混合区，后面 tangent_window 像素是切线计算要用的窗口还没凑齐）
                if (walk < trim + tangent_window)
                    continue;
                // 当前点的切线向量，用前后各 tangent_window 个点相减得到（跨 6 个点取，抹平栅格台阶）
                const auto tangent = point(k + tangent_window) - point(k - tangent_window);
                // 这段切线属于哪条边，跟两个指导方向比点积的绝对值（绝对值是因为切线朝哪个方向无所谓，只看它平行于哪条边），大的那个就是归属
                int label =
                    std::abs(tangent.dot(directions[1])) > std::abs(tangent.dot(directions[0]));
                if (label != group) // 这个点属于另一条边（邻边平行于另一条指导方向，所以一切线方向就翻组、触发 break）
                {
                    if (started)
                        break; // 如果已经开始收集了（started），说明这条边走完了，break；“当前边 → 拐到邻边”就停
                    continue; // 如果还没开始，说明还在转折区，continue 跳过
                }
                started = true;
                support.push_back(p); // 这个点属于这条边，收进来
            }
            // tail 循环：从 support 末尾往回裁掉 trim（2 像素）——末端也可能混进下一个角的转折点
            double tail = 0;
            while (support.size() > 1 && tail < trim)
            {
                tail += cv::norm(support.back() - support[support.size() - 2]);
                support.pop_back();
            }
            // 少于 2 个点、或者首尾重合（撑不起一条直线），失败 "NO_LINE_SUPPORT"
            if (support.size() < 2 || support.front() == support.back()) // support.back()：最后一个元素;support.front()：第一个元素
                return fail("NO_LINE_SUPPORT");
            // 对收集到的点做最小二乘直线拟合，存进 lines[side]
            // cv::fitLine——给一堆点拟合一条直线
            // void cv::fitLine(InputArray points, OutputArray line, int distType, double param, double reps, double aeps);
            // points：输入的点集；line：输出的直线，类型是 Vec4d（(vx, vy, x0, y0)——(vx, vy) 是直线的单位方向向量，(x0, y0) 是直线上的一点（拟合点集的中心））
            // distType：拟合用的距离准则；DIST_L2 是最小二乘（让所有点到直线的垂直距离的平方和最小）；别的取值（DIST_L1、DIST_HUBER 等）是抗 outliers 的鲁棒准则，这里不需要
            // param：某些距离准则用的参数，DIST_L2 用不上，填 0
            // reps、aeps：拟合迭代的精度阈值（半径和角度），这里都是 0.01，改变量小于它就停
            cv::fitLine(support, lines[side], cv::DIST_L2, 0, 0.01, 0.01);
        }

        // 求交点：两条拟合直线求交点，校验不平行、交点有限，返回
        // 两条拟合直线求交点，这个交点就是精修后的角点
        // 拆直线:
        // 从 Vec4d 的 (vx, vy, x0, y0) 布局里拆出来——a 是直线 0 上的一点，v 是直线 0 的方向；b 是直线 1 上的一点，w 是直线 1 的方向
        cv::Point2d a(lines[0][2], lines[0][3]), b(lines[1][2], lines[1][3]);
        cv::Point2d v(lines[0][0], lines[0][1]), w(lines[1][0], lines[1][1]);
        // 平行检查
        // std::numeric_limits<T>（<limits>）是查一个数值类型属性的模板
        // std::numeric_limits<double>::epsilon() 是 double 的机器精度（约 2.2e-16），这里当“接近 0”的阈值用（拦“两条线平行”）
        // isfinite拦住“叉积本身是垃圾”（NaN 和无穷大）
        double cross = v.cross(w);
        if (!std::isfinite(cross) || std::abs(cross) <= std::numeric_limits<double>::epsilon())
            return fail("PARALLEL_LINES"); // |cross| 接近 0 就说明两条线(v、w 都是单位向量，cross = sinθ（θ 是两条直线夹角）)几乎平行，交点跑到无穷远去了，没法算——失败 "PARALLEL_LINES"
        // 求交点：
        // 交点在直线 0 上，写成 a + t*v，t 是待求的倍数
        // 解法是叉积消元：交点也得在直线 1 上，所以 a + t*v = b + s*w，两边同时跟 w 做叉积
        // w 跟自己的叉积是 0，s 那项就消掉了，剩下 t * (v×w) = (b-a)×w，所以 t = (b-a).cross(w) / cross，再回代得到交点坐标
        const auto intersection = a + v * ((b - a).cross(w) / cross);
        if (!std::isfinite(intersection.x) || !std::isfinite(intersection.y)) // 再验一次交点坐标有限，否则 "NONFINITE_INTERSECTION"
            return fail("NONFINITE_INTERSECTION");
        return intersection; // 没问题就 return intersection——精修角点
    }

}
