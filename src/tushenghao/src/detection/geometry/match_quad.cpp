// 四边形匹配：拿上一步找出来的白片，按空间位置把它们归组，确认是不是 MARK 的“三 L 一短 M”结构——是的话就输出一组粗四角（原图坐标），交给下一步 decode_stage 去精修
// 按白片空间邻接归组，确认三 L 一短 M 结构，输出原图坐标的粗四角。
#include "geometry/match_quad.hpp"
#include "geometry/contour_topology.hpp"
#include <algorithm>

namespace mark::detector
{
    namespace
    {
        // 多尺度拟合的比例因子，分别是 1、0.5、0.75、1.25、1.5、2；用来多尺度拟合轮廓，找出四边形
        constexpr std::array<double, 6> approximation_scales{1.0, 0.5, 0.75, 1.25, 1.5, 2.0};

        // 检查每个白片的角是不是合法的拓扑（多尺度去拟合、看转角）；多尺度简化白片轮廓，看它能不能变成“六边一凹、外角和凹点相对”的形状
        // L/M均为六边一凹；外角与凹点在轮廓上相隔三顶点，不能用短笔画端点冒充外角。
        // 查结构：验一个白片在某个角上是不是真的 MARK 角
        // MARK 的角（L 形和 M 形）有个固定形状：简化成多边形后是六边形、恰好一个凹点，而且外角和凹点在六边形上正好隔三个顶点（对面）
        bool corner_topology(const WhiteComponent &component, const GeometryConfig &config,
                             cv::Point corner)
        {
            for (double fraction : approximation_scales) // 按 approximation_scales 的 6 个尺度逐个试（只要有一个尺度能验出合法拓扑，就返回 true；6 个都不行才返回 false）
            {
                const double epsilon = config.approximation_epsilon_ * fraction;
                std::vector<cv::Point> simple;
                cv::approxPolyDP(component.contour_, simple, epsilon, true); // 把白片的轮廓（findContours 出来的原始像素轮廓，点很多、带噪声）用 approxPolyDP 简化成多边形 simple
                // 从 simple 里抠出候选六边形（corner_topology_polygons）
                // simple 正好 6 个点就直接用；7 个点就逐个试着删一个，只留删完还贴合的；其他点数就没有候选
                for (const auto &p : corner_topology_polygons(component.contour_, simple, epsilon))
                {
                    // 对每个候选六边形，准备记 6 个顶点的转角（turns）、凹点个数（concave）、有没有平角（flat）
                    std::array<double, 6> turns;
                    int concave = 0;
                    bool flat = false;
                    for (size_t i = 0; i < 6; ++i)
                    {
                        // 逐个顶点算转角（normalized_turn）：正数是凸（左转）、负数是凹（右转）、0 是平的（180°，三点一线）。数凹点个数，记有没有平角
                        const double turn = turns[i] = normalized_turn(p, i); // 链式赋值
                        concave += (turn < 0);
                        flat |= (turn == 0); // bool 的或累积(只要有一个顶点 turn == 0，flat就变成 true，而且一旦变 true 就一直是 true（“只要有一个平角就记下来”）)
                    }
                    // 凹点不是正好 1 个、或者有平角，这个候选六边形就不合格，跳过
                    if (concave != 1 || flat)
                        continue;
                    // 在合格的六边形里找两个顶点——notch（转角为负的那个凹点），outer（离传入的 corner 这个角最近的那个顶点，也就是理应是 MARK 外角的那个）
                    size_t outer = 0, notch = 0;
                    for (size_t i = 0; i < 6; ++i)
                    {
                        if (turns[i] < 0)
                            notch = i;
                        // cv::norm 签名：double cv::norm(InputArray src, int normType = NORM_L2)；算一个向量/矩阵的范数
                        // 只传一个参数、默认 NORM_L2 时，就是欧氏长度：对 Point2d(x, y) 是 sqrt(x² + y²)
                        if (cv::norm(p[i] - cv::Point2d(corner)) <
                            cv::norm(p[outer] - cv::Point2d(corner)))
                            outer = i;
                    }
                    // 如果 outer 正好是 notch 往后数 3 个顶点（(notch + 3) % 6，六边形的对面，顺序上游保证），结构对上了，返回 true
                    if (outer == (notch + 3) % 6)
                        return true;
                }
            }
            return false;
        }

        // 判断两个白片的外接矩形挨不挨着，用来把白片归组（用相对尺寸，自适应）
        bool adjacent(const cv::Rect &a, const cv::Rect &b)
        {
            // std::max 有个接受花括号列表的重载（<algorithm>），返回列表里最大的那个
            // 注意这里三个数都是 int，先在 int 里比大小，再转成 double 存进 dx
            const double dx = std::max({0, a.x - b.br().x, b.x - a.br().x}); // 算两个矩形在横向上的间距（重叠时为0）
            const double dy = std::max({0, a.y - b.br().y, b.y - a.br().y}); // 算两个矩形在纵向上的间距（重叠时为0）
            const double span = std::max({a.width, a.height, b.width, b.height}); // 取两个矩形的最大边长作为“挨着”的阈值（宽、高里最大的那个）；用白片自己的尺寸当标尺
            // std::hypot：<cmath> 里的，std::hypot(x, y) 返回 sqrt(x² + y²); 求直线距离
            // 两个白片之间的距离（空隙），不超过最大的那个白片的尺寸，就判为挨着(近到像同一个 MARK 的零件)
            // 拿白片尺寸当标尺的好处是它会跟着距离走——MARK 离相机近、白片大时，允许的间隔也大；离得远、白片小时，间隔也得小；这样不管远近，归组的标准都合理
            return std::hypot(dx, dy) <= span; // 算斜边长（两个矩形之间最短的直线距离（dx、dy 是直角边））；如果这个距离不超过 span（最大的那条边），就判为“挨着”
        }

    } // namespace

    // 拿白片列表 → 按相邻归组 → 每组拼成候选四边形 → 逐角验拓扑 → 合格的输出成 MatchQuad
    std::vector<MatchQuad> match_quads(const PreparedFrame &frame,
                                       const std::vector<WhiteComponent> &components,
                                       const GeometryConfig &config)
    {
        // 归组：把挨在一起的白片收进一组（一个 MARK 是由好几个挨着的白片组成的）;得到members(每个 members 就是一坨挨在一起的白片，相当于一个候选 MARK 的零件堆)
        std::vector<bool> visited(components.size(), false); // 给每个白片立一个“已分组？”的标记，一开始全是 false（都没分过组）
        std::vector<MatchQuad> result; // 造个空列表，装最后合格的四边形
        for (size_t seed = 0; seed < components.size(); ++seed) // 把每个白片轮流当一次“种子”，从它开始组一组
        {
            if (visited[seed])
                continue;  // 这个种子已经被分过组了，跳过（不重复组）
            std::vector<size_t> members{seed}; // 新开一组，成员先只有种子自己（这里存的是白片在列表里的**下标**，不是白片本身）
            visited[seed] = true; // 将种子标记为已分组
            // 实测局部归组；S可作连通桥，但不产生模型身份或绑定搜索。
            // 遍历组里已有的成员（注意 members.size() 每次循环都重新算，而且组在循环里还会长大，所以这会一直扩到再也加不进新成员为止（广度优先扩散））
            for (size_t k = 0; k < members.size(); ++k) 
                for (size_t j = 0; j < components.size(); ++j)  // 对每个组内成员，再扫一遍所有白片
                    // 如果这个白片还没分过组，并且它的外接矩形跟组内某个成员的外接矩形“挨着”（adjacent 判定），就把它收进来
                    if (!visited[j] &&
                        adjacent(components[members[k]].bounding_box_, components[j].bounding_box_))
                    {
                        visited[j] = true;
                        members.push_back(j);  // 标记它已分组，并追加进组里
                    }

            // 把这组白片拼成一个凸四边形——拼不成、不是凸四边形，就说明这组不是 MARK，直接扔掉；拼成了，就得到粗四角 q，交给第三块逐角验证
            // 把组里所有白片的轮廓点拼起来、求凸包、再多尺度拟合出四边形，最后验证和方向归一化
            std::vector<cv::Point> pixels, hull, q; // 造三个点列——pixels（拼起来的所有轮廓点）、hull（凸包）、q（拟合出的四边形）；cv::Point 是整型二维点（x、y 都是 int）
            // 把组里每个白片的轮廓点全倒进 pixels，拼成一堆点（组里所有白片轮廓点的总和）
            for (auto i : members)
                pixels.insert(pixels.end(), components[i].contour_.begin(),
                              components[i].contour_.end()); // insert 的区间重载（insert(位置, 首, 尾)：把 [首, 尾) 这个区间里的元素，一次性插（拷贝）到指定位置）
            if (pixels.size() < 3) // 点不到 3 个成不了形状，这组直接跳过
                continue;
            // cv::convexHull：算点集的凸包（能包住所有点的最小凸多边形）
            // 签名 convexHull(点集, 输出, clockwise=false, returnPoints=true)；对 pixels 求凸包，结果存进 hull
            // clockwise（输出顺时针还是逆时针，默认逆时针）、returnPoints（true 输出点本身，false 输出这些点在原数组里的下标）
            cv::convexHull(pixels, hull);
            // 多尺度拟合出四边形:按 approximation_scales 里的 6 个尺度逐个试拟合（approxPolyDP），拟合出正好 4 个点就停(“这坨东西能不能看成一个四边形”)
            for (double fraction : approximation_scales)
            {
                // cv::approxPolyDP：用 Douglas-Peucker 算法把一条曲线化简成顶点更少的多边形（算法是递归的：看首尾连线，找离这条线最远的点，超过 epsilon 就保留（直线近似失效）、再分两半递归）
                // 签名 approxPolyDP(曲线, 输出, epsilon, closed)
                // epsilon 是允许的最大偏差：拟合(化简)多边形与原曲线的距离不超过它。epsilon 越小拟合越精细、点越多；越大越粗糙、点越少
                // closed=true 表示这是闭合多边形（首尾相连），false 是开放折线
                cv::approxPolyDP(hull, q, config.approximation_epsilon_ * fraction, true);
                if (q.size() == 4)
                    break;
            }
            // cv::isContourConvex(q)：判断一个轮廓是不是凸的，返回 bool
            // 做法是逐个顶点看转向：凸多边形的所有转弯都朝同一个方向（全左转或全右转），只要有一个反向就不是凸的
            if (q.size() != 4 || !cv::isContourConvex(q)) // 拟合完还不是 4 个点，或者不是凸的，说明这组不是 MARK 的四边形，直接跳过
                continue;
            // 如果四边形是顺时针的（面积为负），把它翻成逆时针，统一方向，方便后面按顺序处理(兜底，理论不会出现顺时针的，因为凸包是逆时针的)
            // cv::contourArea(轮廓, oriented)：算轮廓围成的面积，用鞋带公式
            // oriented=false（默认）返回绝对面积，恒为正；oriented=true 返回有向面积，逆时针为正、顺时针为负——符号告诉你绕行方向，绝对值是面积大小
            if (cv::contourArea(q, true) < 0)   
                std::reverse(q.begin(), q.end()); // std::reverse(首, 尾)：把 [首, 尾) 这个区间的元素顺序原地倒过来，两头往中间换(任何双向迭代器都能用，代价是线性（n/2 次交换）)
            
            // 逐角验证：每个角都要在它所属的白片上，并且这个白片的拓扑要合法（L/M 均为六边一凹，外角与凹点在轮廓上相隔三顶点，不能用短笔画端点冒充外角）
            // 逐角验证——对拟合出的四边形的 4 个角，一个一个验，看它是不是真的 MARK
            // 给四边形的每个角找主人、验身份、量长短，最后只认“三长一短”的 MARK
            // 先立三个记账的东西
            std::array<size_t, 4> owner{}; // 每个角属于哪个白片（std::array<T, N>：固定长度的数组（C++11）；4 个元素都值初始化为 0）
            int short_count = 0; // 有几个“短”角
            bool valid = true; // 这组目前合不合格
            // 逐角循环（i = 0 到 3）
            for (size_t i = 0; i < 4; ++i)
            {
                // 找主人——这个角属于哪个白片(角不能悬空，必须在某个白片上)
                double best = INFINITY; // best 先设成无穷大（还没找到）；INFINITY：<cmath> 里的宏，正无穷大（浮点），任何有限数都比它小，用来初始化“目前最优”
                for (auto j : members)
                {
                    // 对组里每个白片，算它的轮廓到这个角 q[i] 的距离（pointPolygonTest，取绝对值，不管点在轮廓内还是外，只看多远）
                    // 判断一个点和轮廓的位置关系cv::pointPolygonTest：签名 double cv::pointPolygonTest(InputArray contour, Point2f pt, bool measureDist)
                    // contour：轮廓（一串点）；pt：要测的点；measureDist：要不要量距离
                    // measureDist=false 时只返回 +1（在内）、−1（在外）、0（在边界上）；measureDist=true 时返回有向距离：正数表示点在轮廓内、负数在外、0 在边界上，绝对值是到最近一条边的距离
                    const double distance =
                        std::abs(cv::pointPolygonTest(components[j].contour_, q[i], true));
                    // 距离最小的那个白片，就是这个角的“主人”，下标记进 owner[i]
                    if (distance < best)
                        best = distance, owner[i] = j;
                }

                // 查重名——两个角不能认同一个主人
                for (size_t j = 0; j < i; ++j)
                    if (owner[j] == owner[i]) // 拿当前角的 owner[i]，跟前面已经验过的角（0 到 i-1）比
                        valid = false; // 如果有相同的，说明两个角指着同一块白片，valid 记 false（整个四边形都坏掉）

                // 验角本身——主人得是合格的角
                const auto &component = components[owner[i]]; // 取出主人白片 component
                if (component.touches_border_ || !corner_topology(component, config, q[i])) // 主人碰图边（touches_border_，可能被裁切、不完整），或者过不了 corner_topology（这个白片在这个角的位置上没有合法的角结构）
                {
                    valid = false;
                    continue; // 不过关就 valid = false，continue 跳过这个角后面的量长短（角坏了，这组已经废了）
                }
                // 量长短——这个角的臂是长是短(MARK 四个角里三个是长臂（30mm）、一个是短臂（14mm），得量出每个角的臂长，才能数出有几个短角)
                double span = 0;
                for (size_t j : {(i + 3) % 4, (i + 1) % 4}) // 对这个角 q[i]，看它两条相邻边——指向前一个角 (i+3)%4 和后一个角 (i+1)%4 的方向
                {
                    const cv::Point2d d = cv::Point2d(q[j]) - cv::Point2d(q[i]); // d 是其中一条边的向量（从这个角指向邻角）
                    double lo = INFINITY, hi = -INFINITY;
                    for (auto p : component.contour_)
                    {
                        // 把主人白片轮廓上的每个点 p，都投影到 d 这个方向上：t = (p - q[i])·d / (d·d)；（点积）
                        // t 是个比值——t=0 是在角上，t=1 是到了邻角的位置，t 在 0 到 1 之间就是在边上，超出就是伸出去了
                        // Point2d::dot：double dot(const Point2d& other) const，点积：x1*x2 + y1*y2
                        // 几何意义是一个向量在另一个向量方向上的分量
                        // 投影公式：分子是 (p − q[i]) 沿 d 方向的分量乘以 |d|，分母是 |d|²，两者一除就得到沿边方向的比值——t=0 在角上，t=1 到了邻角，0 到 1 之间是在边上
                        const double t = (cv::Point2d(p) - cv::Point2d(q[i])).dot(d) / d.dot(d);
                        // 对这条边，把所有点的 t 取最小 lo 和最大 hi
                        lo = std::min(lo, t);
                        hi = std::max(hi, t);
                    }
                    // hi - lo 就是这块白片沿这条边方向伸出去的总长度（以边长为单位，是比值，不是像素）
                    // 两条边都量，取大的那个当这个角的 span
                    span = std::max(span, hi - lo);
                }
                // 图纸30mm与14mm外臂的中点22mm；归一化到80mm框边，这里只确认三L一M，不分配物理身份。（MARK 框边是 80mm，长臂是 30mm、短臂是 14mm）
                // 22 是 30 和 14 的中点，80 是 MARK 框边长，所以 22/80 = 0.275 是长短的分界线。小于它就是短角，short_count 加一
                if (span < 22.0 / 80)
                {
                    ++short_count;
                }
            }
            // 循环结束后的总闸：
            // valid 必须还是 true，而且 short_count 必须正好是 1（“三 L 一短 M”（三个长角、一个短角））；不满足，这组直接扔掉
            if (!valid || short_count != 1)
                continue;
            // 过关输出
            // 把 4 个角从工作图坐标换算回原图坐标，装成一个 MatchQuad，推进 result
            MatchQuad match;
            for (int i = 0; i < 4; ++i)
                match[i] = frame.work_to_original(q[i]);
            result.push_back(match);
        }
        return result;
    }

} // namespace mark::detector
