// PnP几何与数值合法性；独立复算重投影RMSE。
#include "validation/pose_geometry.hpp"
#include <vector>

#include <algorithm>
#include <cmath>
#include <limits>
#include <opencv2/calib3d.hpp>

namespace mark::pose
{

    namespace detail
    {
        // 4 个 2D 点能不能构成一个合法四边形——点要有限、在图内、严格凸。合法返回 true，不合法返回 false，是送进 solvePnP 之前的门
        // 4 个 2D 点能否构成合法四边形（在图内、凸、不退化）
        // 原图四角必须有限、在画面内、严格凸且按相邻物理角成环；退化图形不送 OpenCV。
        bool valid_quad(const std::array<cv::Point2d, 4> &p, cv::Size size)
        {
            // 验图尺寸：图宽高必须大于 0，不然连“在图内”都没法判，直接 false
            if (size.width <= 0 || size.height <= 0)
                return false;
            
            // 逐点验 + 凸性检查
            double sign = 0;
            for (std::size_t i = 0; i < p.size(); ++i)
            {
                // 先验这个点本身
                // x、y 必须有限（isfinite），且在图内（0 <= x < width，0 <= y < height）。出界、非有限，直接 false
                if (!std::isfinite(p[i].x) || !std::isfinite(p[i].y) || p[i].x < 0 || p[i].y < 0 ||
                    p[i].x >= size.width || p[i].y >= size.height)
                    return false;
                // 再验这个点处的转弯
                // 取连续两条边——a = p[i+1] - p[i]（从 i 到 i+1 的边），b = p[i+2] - p[i+1]（从 i+1 到 i+2 的边），% 4 是收尾成环
                const auto a = p[(i + 1) % 4] - p[i];
                const auto b = p[(i + 2) % 4] - p[(i + 1) % 4];
                // cross = a.cross(b) 是这两条边的叉积，符号就是在顶点 i+1 处的转弯方向（左转正、右转负，或者反过来，取决于坐标系，关键是四个顶点方向一致）
                const double cross = a.cross(b);
                // 三个不合格条件，任一成立就 false：
                // !isfinite(cross)——叉积算出来不是有限数（点太大、溢出）；
                // cross == 0——两条边共线，三个点在一条直线上，这个顶点退化了；
                // i > 0 && cross * sign <= 0——从第二个顶点起，转弯方向必须跟上一个顶点同号。cross * sign <= 0 就是符号翻了（凹进去或者自交）
                // i > 0是因为第一个顶点还没有“上一个”可比，sign 初始是 0，不跳过就误判
                if (!std::isfinite(cross) || cross == 0 || (i > 0 && cross * sign <= 0))
                    return false;
                sign = cross; // 每验完一个顶点，sign = cross，把本次转弯方向记下来给下一个顶点比
            }
            // 四个顶点全过，返回 true（单纯返回 bool，不排序）
            return true;
        }

        // 对一个 PnP 解做独立复核
        // 对 PnP 结果的独立复核——拿算出的位姿把 3D 点重投影回图像，跟观测的 2D 点比，算 RMSE ，检查正深度。不信求解器自己报的误差，自己重算一遍。
        // OpenCV 自带误差的口径易混淆；统一复算每点二维欧氏误差的 RMS。
        std::optional<PoseCandidate> evaluate_pose(const RigidPose &pose_,
                                                   const std::array<cv::Point3d, 4> &object,
                                                   const std::array<cv::Point2d, 4> &image_,
                                                   const cv::Mat &K_, const cv::Mat &dist_)
        {
            // 准备
            PoseCandidate out;
            out.pose_ = pose_; // 把待验证的位姿先拷进输出结构体
            // std::numeric_limits<T> 是个模板，给出数值类型的各种属性（最小值、最大值、epsilon、无穷大等）。double 的 infinity() 就是 +inf
            double min_depth = std::numeric_limits<double>::infinity(); // min_depth 初始化为正无穷（std 的 API，返回 double 类型的正无穷大），这样第一个真实深度一定比它小，std::min 才能正常工作

            // 有限性门（求解器数值崩了会吐出这种值，后面没法用）
            // rvec 和 t_m 的 6 个数逐个查 isfinite，有一个是 NaN 或 inf 就直接 return {}
            for (int i = 0; i < 3; ++i)
                if (!std::isfinite(pose_.rvec_[i]) || !std::isfinite(pose_.t_m_[i]))
                {
                    return {};
                }

            // 深度门（点必须在相机前方）
            // 把 3D 点按位姿变换到相机系，检查 z 分量（深度）是否都大于 0。先把 Rodrigues 向量转成旋转矩阵 r，再算 r * p + t，取 z 分量。
            cv::Matx33d r;
            // Rodrigues 把 rvec 转成 3×3 旋转矩阵 R（旋转矩阵和旋转向量互相转换）
            // 完整签名：void cv::Rodrigues(InputArray src, OutputArray dst, OutputArray jacobian = noArray())
            // src：输入，旋转向量（3×1 或 1×3）或旋转矩阵（3×3）。这里传的是 pose_.rvec_（Vec3d，3×1 旋转向量）
            // dst：输出，旋转矩阵（3×3）或旋转向量（3×1 或 1×3），跟输入反过来。这里是 r（Matx33d），输出 3×3 旋转矩阵
            // jacobian：可选输出，雅可比矩阵（3×9 或 9×3），是输出各分量对输入各分量的偏导数。没传，默认 noArray()，不需要
            // 方向由输入类型决定：输向量出矩阵，输矩阵出向量；这里rvec → R
            cv::Rodrigues(pose_.rvec_, r);
            // 4 个模型点逐个变到相机系 (R·p + t)，取 [2] 分量（z = 深度），记最小值
            for (const auto &p : object)
                min_depth = std::min(min_depth, (r * cv::Vec3d(p.x, p.y, p.z) + pose_.t_m_)[2]); // ！！！OpenCV 重载了运算符，可以矩阵运算（尺寸要对上）：+、-：同尺寸矩阵/向量相加减；*：矩阵乘法（Matx×Matx、Matx×Vec、Vec×Matx、矩阵×标量）；/：除以标量；==、!=：比较；[]、()：取元素；一些方法：转置 .t()、求逆 .inv()、点积 .dot()、叉积 .cross()（Vec3d）、逐元素乘 .mul()、范数 .norm()；特别注意： * 是矩阵乘法，不是逐元素乘，也不是点积。逐元素乘用 .mul()，点积用 .dot()
            // min_depth <= 0 意味着至少一个点在相机后面（或正好在相机上）——可见的 MARK 不可能这样，直接拒掉
            if (!(min_depth > 0)) // 写成 !(min_depth > 0) 还顺带拒了 NaN（NaN 比较全是 false，取反就是 true）
            {
                return {};
            }

            // 重投影
            std::vector<cv::Point2d> projected;
            // projectPoints：4 个模型点按这个位姿（rvec、t）+ K + dist，正向投影回图像，得到预测的像素点 projected（把 3D 点投影到图像平面上）
            // 这是 solvePnP 的逆过程——求解是像素 → 位姿，这里是位姿 → 像素，用来验证
            // 完整签名：void cv::projectPoints(InputArray objectPoints, InputArray rvec, InputArray tvec,
            //    InputArray cameraMatrix, InputArray distCoeffs, OutputArray imagePoints,
            //    OutputArray jacobian = noArray(), double aspectRatio = 0)
            // objectPoints：3D 点数组，3×N 或 N×3（或 vector），N 是点的个数。这里是 4 个模型点
            // rvec：旋转向量，就是 pose_.rvec_；tvec：平移向量，就是 pose_.t_m_；cameraMatrix：相机内参矩阵 K，就是 K_；distCoeffs：畸变系数向量，4、5、8、12 或 14 个元素；如果为空，就当作零畸变。就是 dist_
            // imagePoints：输出的图像点数组，2×N 或 N×2（或 vector）。就是 projected，输出的 4 个预测像素点
            // jacobian：可选输出，2N×(10+畸变系数个数) 的雅可比矩阵，是图像点对旋转向量、平移向量、焦距、主点、畸变系数的偏导数。没传，默认 noArray()
            // aspectRatio：可选的“固定长宽比”参数。如果不为 0，就假定 fx/fy 的比值固定，并相应调整雅可比矩阵。没传，默认 0
            // 内部链条：世界坐标 →（经 rvec/tvec）→ 相机坐标 → 归一化相机坐标 → 加畸变 →（经 cameraMatrix）→ 像素坐标（也就是 solvePnP 的逆过程）！
            cv::projectPoints(std::vector<cv::Point3d>(object.begin(), object.end()), pose_.rvec_,
                              pose_.t_m_, K_, dist_, projected); // 3D 点按位姿投影回图像(重算误差)
            
            // 算 RMSE
            double sum = 0;
            // 每个点算预测与观测的二维欧氏距离（cv::norm），距离非有限就拒掉
            for (std::size_t i = 0; i < image_.size(); ++i)
            {
                const double e = cv::norm(projected[i] - image_[i]);
                if (!std::isfinite(e))
                {
                    return {};
                }
                sum += e * e;
            }
            // 平方求和、除以 4、开方，就是 out.rmse_px_（以像素为单位的 RMSE）
            out.rmse_px_ = std::sqrt(sum / image_.size());
            // RMSE 本身非有限也拒掉
            if (!std::isfinite(out.rmse_px_))
                return {};
            // 都通过才 return out
            return out;
        }

    } // namespace detail
} // namespace mark::pose
