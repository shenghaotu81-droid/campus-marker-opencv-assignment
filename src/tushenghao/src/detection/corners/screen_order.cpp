#include "vision/types/corner_types.hpp"
#include <algorithm>
#include <cmath>

namespace mark::detector
{
    // 拿 4 个乱序的角点，先验证它们是个合格的凸四边形，再排成统一的屏幕顺序（顺时针：左上、右上、右下、左下）；不合格就返回空 + 原因
    // 先验货（凸四边形才收），再按“上左上右、下右下左”排成顺时针
    std::optional<CornerPoints> order_screen_corners(const CornerPoints &p, std::string &reason_)
    {
        // 验证
        // 先清掉 reason_，sign 用来记上一个顶点的转向
        reason_.clear();
        double sign = 0;
        for (size_t i = 0; i < 4; ++i)
        {
            // 每个点先查是不是有限数（NaN/无穷大就 NON_FINITE_CORNER）
            // std::isfinite(x)（<cmath>）：x 是不是有限数——不是 NaN、不是正负无穷大就返回 true
            if (!std::isfinite(p[i].x) || !std::isfinite(p[i].y))
            {
                reason_ = "NON_FINITE_CORNER";
                return {};   // 等价于 return std::nullopt;（把返回类型值初始化，空的 optional）
            }
            // 再算叉积：(p[i+1]-p[i]) 和 (p[i+2]-p[i+1]) 两条相邻边的叉积，就是顶点 i+1 处的转向
            // Point2d::cross：a.cross(b) = a.x*b.y - a.y*b.x，二维叉积；一般规律：cross = |a||b|·sinθ，符号就是转向（正左转、负右转、0 共线）
            double cross = (p[(i + 1) % 4] - p[i]).cross(p[(i + 2) % 4] - p[(i + 1) % 4]);
            // 叉积非有限、等于 0（三点共线、没有转角）、或者从第二个顶点起转向（符号）跟上一个不一致（(cross > 0) != (sign > 0)），都是 DEGENERATE_QUAD（四个顶点的转向必须同号，才是个凸四边形）
            if (!std::isfinite(cross) || cross == 0 || (i && (cross > 0) != (sign > 0)))
            {
                reason_ = "DEGENERATE_QUAD";
                return {};
            }
            sign = cross; // 每轮把 sign 更新成当前叉积，给下一个顶点比
        }

        // 排序
        // 拷一份 q，按 y 排（y 小的在前=靠上），y 相同按 x 排——排完 q[0]、q[1]是上面两个点，q[2]、q[3] 是下面两个点
        auto q = p;
        // std::sort(first, last, comp)（<algorithm>）：把 [first, last)这一段按 comp 排好序；sort 内部不断调用comp(a, b) ，返回 true 就把 a 排在 b 前面
        // 这里 comp 是“y 小的在前，y 相同 x 小的在前”（“a 排在 b 前面的条件”）
        // lambda [](auto a, auto b){...}：一个内联的匿名函数；这里它是传给 std::sort 的比较器
        // [] 是捕获列表（空=什么都不捕获），(auto a, auto b) 是参数（auto 让编译器推断类型，这里是 Point2d），大括号里是函数体
        std::sort(q.begin(), q.end(),
                  [](auto a, auto b)
                  {
                      return a.y < b.y || (a.y == b.y && a.x < b.x); // 先比 y （主规则）再比 x（从上到下，如果平局从左到右）
                  });
        // 上面一对：如果 q[0] 在 q[1] 右边，交换——保证 q[0] 是左上、q[1] 是右上
        if (q[0].x > q[1].x)
            std::swap(q[0], q[1]);
        // 下面一对：如果 q[2] 在 q[3] 左边，交换——保证 q[2] 是右下、q[3] 是左下
        if (q[2].x < q[3].x)
            std::swap(q[2], q[3]);
        // 最后顺序是 [左上，右上，右下，左下]，顺时针绕一圈
        // std::optional ：return {} 是空，if (opt) 是有没有值，*opt 是取值
        return q;
    }

}
