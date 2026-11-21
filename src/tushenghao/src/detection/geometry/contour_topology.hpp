#pragma once
#include <algorithm>
#include <opencv2/imgproc.hpp>

namespace mark::detector
{

    // 点到有限线段；裁剪的是内部投影参数，不是检测角坐标。
    // 算点 p 到有限线段 ab 的距离；注意是线段（有端点），不是无限长的直线
    // 段内：点到直线的垂线距离；段外：改成离哪个端点近就量到哪个端点的距离
    inline double segment_distance(cv::Point2d p, cv::Point2d a, cv::Point2d b)
    {
        auto v = b - a; // 线段的方向向量（从 a 指向 b）
        double n = v.dot(v); // 线段长度的平方（|v|²），后面算投影比例时当分母
        if (!(n > 0)) // 如果 n 不大于 0，说明 a 和 b 是同一个点、线段退化成一个点，距离就是 p 到 a 的直线距离，直接返回（防御）
            return cv::norm(p - a);
        // 主计算
        // 投影：(p - a).dot(v) / n 是投影参数 t，表示 p 投在线段方向上的位置：t=0 在 a 处，t=1 在 b 处，0 到 1 之间落在段内，小于 0 在 a 之前，大于 1 在 b 之后
        // 裁剪：std::clamp(t, 0.0, 1.0) 把 t 夹到 [0, 1]，投影落在段外就按最近的端点算（“有限线段”和无限直线的区别）
        // a + v * t 是线段上离 p 最近的点，p 减它再取 cv::norm，就是 p 到线段的距离
        // std::clamp（C++17，<algorithm>）签名：template<class T> constexpr const T& std::clamp(const T& v, const T& lo, const T& hi);
        // 把 v 夹到 [lo, hi] 这个区间里：v 小于 lo 返回 lo，大于 hi 返回 hi，在区间里就返回 v 本身
        return cv::norm(p - (a + v * std::clamp((p - a).dot(v) / n, 0.0, 1.0)));
    }

    // 面积绕向归一化凹凸，反向遍历轮廓也保持同一结构身份。
    // 算多边形 p 在第 i 个顶点处的转角，判断这个顶点是凸、凹还是平
    // 不管多边形是顺时针还是逆时针绕的，结果的符号都一样（正=凸，负=凹，0=平）
    inline double normalized_turn(const std::vector<cv::Point2d> &p, size_t i)
    {
        auto a = p[i] - p[(i + p.size() - 1) % p.size()], b = p[(i + 1) % p.size()] - p[i]; // a 是进边向量（前一个顶点 → 当前顶点），b 是出边向量（当前顶点 → 下一个顶点）
        double area = 0;
        // 算整个多边形的有向面积（鞋带公式求和）
        // 对每条边（从 p[j] 到 p[(j+1)%size]），累加 q.x * r.y - q.y * r.x
        // 加完的 area是有向面积的两倍，符号就是绕向：正数=逆时针，负数=顺时针。
        for (size_t j = 0; j < p.size(); ++j)
        {
            auto q = p[j], r = p[(j + 1) % p.size()];
            area += q.x * r.y - q.y * r.x;
        }
        // 转向（叉积）和绕向（面积）
        // (a.x * b.y - a.y * b.x) 是 a 和 b 的叉积 = |a||b|·sinθ，正数是左转、负数是右转、0 是直的
        // 再乘以 area：“归一化”
        // 逆时针绕时，凸顶点左转（叉积正）× 面积正 = 正，凹顶点右转（叉积负）× 面积正 = 负；顺时针绕时，凸顶点右转（叉积负）× 面积负 = 正，凹顶点左转（叉积正）× 面积负 = 负
        // 两种绕向，凸恒正、凹恒负——反向遍历轮廓，同一个顶点的凸凹身份不变
        // 凸：转向和绕向一致；凹：转向和绕向相反；平：三点一线，叉积为 0！！
        return (a.x * b.y - a.y * b.x) * area;
    }

    // 六边L/M的RDP偶尔保留一个额外顶点。只允许从原弧覆盖得到的 7→6 简化。
    // 从简化后的多边形 simple 里抠出候选六边形，返回一个六边形列表（每个六边形是 6 个 Point2d）
    // 6 点直接用，7 点试着删一个、删完还要验算贴合，别的点数一律不要
    inline std::vector<std::vector<cv::Point2d>> corner_topology_polygons(const std::vector<cv::Point> &contour_,
                             const std::vector<cv::Point> &simple, double epsilon)
    {
        std::vector<std::vector<cv::Point2d>> result; // 准备一个空的候选列表 result
        // simple 正好 6 个点，直接把它当唯一候选装进 result，返回
        if (simple.size() == 6)
        {
            result.emplace_back(simple.begin(), simple.end()); // 在 result 末尾直接造一个 vector<Point2d>，用 simple 的 [begin, end) 区间去初始化它
            return result;
        }
        // simple 不是 6 个点、也不是 7 个点，没有候选，返回空的 result
        if (simple.size() != 7)
            return result;
        // 7 个点的情况
        std::vector<size_t> indices;
        // 先给 simple 的每个点在原轮廓 contour_ 里定位(防守)
        for (auto p : simple)
        {
            // std::find 在 contour_ 里找这个点；找不到（it == contour_.end()）说明简化后的点不在原轮廓上，直接返回空；找到了就把它的下标记进 indices
            // std::find 签名：template<class InputIt, class T> InputIt std::find(InputIt first, InputIt last, const T& value);
            // 在 [first, last) 里按顺序找第一个等于 value 的元素，找到返回指向它的迭代器，找不到返回 last
            auto it = std::find(contour_.begin(), contour_.end(), p);
            if (it == contour_.end())
                return result;
            indices.push_back(size_t(it - contour_.begin())); // 迭代器相减得到下标（两个指向同一个 vector 的迭代器相减，得到它俩之间隔了几个元素，也就是下标）
        }
        // 逐个试着删掉 7 个点中的一个（omit 从 0 到 6），每种删法得到一个候选六边形
        for (size_t omit = 0; omit < 7; ++omit)
        {
            // 拼这个候选：
            // 把 simple 里除了 omit 的那个点之外的 6 个点按顺序装进 polygon，同时把它们在原轮廓里的下标装进 selected
            std::vector<cv::Point2d> polygon;
            std::vector<size_t> selected;
            for (size_t i = 0; i < 7; ++i)
                if (i != omit)
                {
                    polygon.push_back(simple[i]);
                    selected.push_back(indices[i]);
                }
            // 验证
            // 对拼好的六边形的每条边，沿原轮廓走一遍这条边对应的弧（从 selected[i] 走到 selected[(i+1)%6]），检查弧上每个轮廓点到这条边的距离都不超过 epsilon
            // 有一条边不过，整个候选就扔掉；6 条边全过，这个六边形才进 result
            bool bounded = true;
            for (size_t i = 0; i < 6 && bounded; ++i)
            {
                // 这条边对应的弧是从 selected[i] 走到 selected[(i+1)%6]，沿着原轮廓走一遍
                size_t begin = selected[i], end = selected[(i + 1) % 6];
                for (size_t k = begin;; k = (k + 1) % contour_.size()) // k = (k+1) % contour_.size() 是让 k 在轮廓上绕圈(回环)走，直到 k == end 就停
                {
                    if (segment_distance(contour_[k], polygon[i], polygon[(i + 1) % 6]) > epsilon) // 先量距离
                    {
                        bounded = false;
                        break;
                    }
                    if (k == end) // 再看是不是到头了（走到 end 就说明这条弧查完了）
                        break;
                }
            }
            // 6 条边全查完、bounded 还是 true，说明每条边都盖住了自己那段弧（这个删掉一个点得来的六边形，确实是原轮廓在 epsilon 内容差的拟合，可以当候选，收进 result）
            if (bounded)
                result.push_back(std::move(polygon)); 
        }
        return result;
    }

}
