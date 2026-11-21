// render_debug_frame，负责在每一帧上画调试信息（检测框、位姿、跟踪轨迹），画完再交给 exporter 写视频
// 准备、检测框、位姿、跟踪、返回
#include "rendering/debug_renderer.hpp"
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <iomanip>
#include <sstream>

namespace mark
{
    // 渲染调试帧：原图 + 检测框 + 位姿 + 跟踪
    cv::Mat render_debug_frame(const cv::Mat &image_, const detector::FrameResult &raw,
                               const std::vector<pose::PoseObservation> &poses_,
                               const std::vector<tracking::TargetTrack> &tracks_,
                               const pose::PoseConfig *config)
    {
        // 准备 + 帧号：克隆原图当画布，定义一个 text helper（在左上角按行写字），先写第 0 行"frame N"
        // 把原图深拷贝一份当画布
        // cv::Mat::clone：Mat clone() const;
        // 深拷贝——真的复制一份像素数据，返回的新 Mat 和原 Mat 互不干扰；对应的是 =赋值（浅拷贝，只拷引用计数，共享像素）。
        cv::Mat out = image_.clone(); // 里必须用 clone()，因为后面要在 out 上画画，不能动原图
        // 在画布左上角按行写字helper
        // 一个 lambda，[&] 捕获（抓到了外面的 out，按引用抓，所以里面画的就是这张画布）；row 是调用方维护的行号（后面每写一行就 row++），这样文字不会重叠
        // cv::putText：void cv::putText(InputOutputArray img, const String& text, Point org, int fontFace, double fontScale, Scalar color, int thickness = 1, int lineType = LINE_8, bool bottomLeftOrigin = false);
        // 在图上写字
        /* 参数：
        img = out：画在哪张图上。
        text = s：写的字符串。
        org = {12, 24 + row * 24}：文字左下角的起点（x=12，y 按行号算）。位置：x 固定 12，第 0 行 y=24，往下每行 +24 像素
        fontFace = FONT_HERSHEY_SIMPLEX：字体（Hershey 字体集里最常用的无衬线体）。
        fontScale = .55：字号缩放（1.0 是基准大小，.55 就是 55%）。
        color：lambda 的参数，调用时传进来。
        thickness = 1：笔画粗细 1 像素。
        lineType = LINE_AA：抗锯齿（Anti-Aliased），字边缘平滑；默认 LINE_8 是锯齿的。
        bottomLeftOrigin 没传，默认 false（原点在左上角，y 向下）。
        */
       // cv::Scalar：Scalar(double v0, double v1 = 0, double v2 = 0, double v3 = 0);
       // 一个 4 维 double 向量，当颜色用时是 BGR 顺序（蓝、绿、红），不是 RGB
       // 所以 {255, 255, 255} 是白色，{0, 255, 0} 是绿色（B=0、G=255、R=0），{255, 255, 0} 是黄色（B=255、G=255、R=0）；第 4 个分量一般不用（透明度，putText 不用它）
       // 常量说明：FONT_HERSHEY_SIMPLEX：Hershey 字体之一，笔画简单的无衬线体，OpenCV 画调试文字最常用；LINE_AA：抗锯齿线型（cv::LINE_AA = 16），画出来边缘平滑；不写就是 LINE_8（8 连通，锯齿）
        auto text = [&](const std::string &s, int row, cv::Scalar color)
        {
            cv::putText(out, s, {12, 24 + row * 24}, cv::FONT_HERSHEY_SIMPLEX, 0.55, color, 1,
                        cv::LINE_AA);
        };
        // 写帧号：第 0 行，白色，写 "frame N"（N 是帧号）；这是每帧调试图的标题行
        text("frame " + std::to_string(raw.frame_id_), 0, {255, 255, 255});

        // 画检测框（绿色）：每个检测到的四边形：四条边连起来、四个角点画实心圆、每个角标序号 0–3
        // 外层循环：raw.detections_ 里的每个检测到的四边形都画一遍
        for (const auto &d : raw.detections_)
            // 内层循环（i = 0–3，四个角）：
            for (int i = 0; i < 4; ++i)
            {
                // cv::line：void cv::line(InputOutputArray img, Point pt1, Point pt2, const Scalar& color, int thickness = 1, int lineType = LINE_8, int shift = 0);
                // 在图上画一条线；这里从角点 i 连到角点 (i + 1) % 4
                /* 参数：
                img = out：画在哪张图上。
                pt1、pt2：线段的两个端点（这里是角点 i 和角点 (i+1)%4）。四边形闭合
                color = {0, 255, 0}：绿色（BGR）。
                thickness = 1：线宽 1 像素。
                lineType = LINE_AA：抗锯齿。
                shift 没传，默认 0（坐标的小数位数移位，0 就是坐标直接当整数像素用）；d.corners_ 是 Point2f（带小数的像素坐标），而 line 要的是 Point（=Point2i，整数像素）——传进去时隐式转换，小数部分直接截掉。画图只要整数像素，截掉这点零头不影响。
                */
                cv::line(out, d.corners_[i], d.corners_[(i + 1) % 4], {0, 255, 0}, 1, cv::LINE_AA);
                // cv::circle：void cv::circle(InputOutputArray img, Point center, int radius, const Scalar& color, int thickness = 1, int lineType = LINE_8, int shift = 0);
                // 在图上画圆；这里在每个角点画个实心圆，半径 3，绿色
                /* 参数：
                img = out，center = d.corners_[i]：圆心是角点。
                radius = 3：半径 3 像素。
                color = {0, 255, 0}：绿色。
                thickness = -1：-1 是个特殊值，表示"填满"（画实心圆，不是描边）。正数就是描边的粗细。
                lineType 没传，默认 LINE_8（锯齿）；shift 默认 0
                */
                cv::circle(out, d.corners_[i], 3, {0, 255, 0}, -1);
                // cv::putText：在每个角点旁边写序号 0、1、2、3，绿色，字号 0.5
                // 注意这里没传 thickness 和 lineType，用的是默认值（粗细 1，LINE_8 锯齿）——和前面 text 那个 lambda 不一样，那个显式传了 LINE_AA
                cv::putText(out, std::to_string(i), d.corners_[i], cv::FONT_HERSHEY_SIMPLEX, 0.5,
                            {0, 255, 0});
            }

        // 画位姿（黄色）：每条位姿观测先写一行字（"MARK: ESTIMATED" 这种）；只有 MARK 且有估计值才把模型点投影回图像、画黄框
        // 黄框的意义（可视化重投影误差rmse_px_）：是"位姿说角点应该在哪"——如果黄框和第 2 块的绿框（检测器实际找到的角点）重合，说明位姿估计准；如果黄框飘了，说明 PnP 算出来的位姿有问题
        int row = 1; // 行号：文字从第 1 行开始写（第 0 行是 "frame N"），每写一条观测就 row++
        for (const auto &p : poses_)
        {
            // 投影只使用当前有效位姿；不会把预测点作为新角点反馈给检测器。（仅可视化）
            // 每条观测先写一行字：黄色，写 "MARK: ESTIMATED" 或 "APRILTAG: UNAVAILABLE"这种；注意这行字是每条观测都写，不管有没有估计值
            text(p.source_ + ": " + pose::pose_status_name(p), row++, {255, 255, 0});
            // 画黄框的条件：没 config（拿不到相机内参和模型点）→ 不画；没 estimate_（UNAVAILABLE 的观测）→ 不画；不是 MARK（TAG 的观测）→ 不画
            // 只画 MARK，因为 config->mark_points_m_ 是 MARK 的模型点（80×80mm 的四个角），TAG 的模型点不在这儿
            if (!config || !p.estimate_ || p.source_ != "MARK")
                continue;
            // 投影 + 画框：
            // cv::projectPoints 把 MARK 的四个 3D 模型点（米制）按这帧估计出的位姿（rvec、t_m）和相机内参（K、dist）投影回图像，得到四个 2D 像素点，然后四条黄线连起来（还是 (i+1)%4 闭合）
            std::vector<cv::Point2d> pixels;
            // cv::projectPoints：void cv::projectPoints(InputArray objectPoints, InputArray rvec, InputArray tvec, InputArray cameraMatrix, InputArray distCoeffs, OutputArray imagePoints, OutputArray jacobian = noArray(), double aspectRatio = 0);
            // 把 3D 点按位姿和相机参数投影回图像
            /* 参数：
            objectPoints：3D 模型点（这里是 std::vector<cv::Point3d>(config->mark_points_m_.begin(), config->mark_points_m_.end())——MARK 的四个角，米制）。
            rvec：p.estimate_->pose_.rvec_——这帧估计出的旋转向量。
            tvec：p.estimate_->pose_.t_m_——这帧估计出的平移向量。
            cameraMatrix：config->camera_.K_——内参矩阵。
            distCoeffs：config->camera_.dist_——畸变系数。
            imagePoints：pixels——输出，投影后的 2D 像素点（std::vector<cv::Point2d>）。
            jacobian、aspectRatio 没传，用默认值（不需要雅可比，长宽比 0）。
            */
           // solvePnP 的逆操作：solvePnP 是"2D 像素 + 3D 模型 → 位姿"，projectPoints 是"3D 模型 + 位姿 → 2D 像素"；用于算重投影误差，拿投影点和检测到的角点比距离
           // std::vector 的区间构造 + begin() / end()：std::vector<cv::Point3d>(first, last);
           // 用两个迭代器 [first, last) 区间里的元素构造一个新 vector
           // 这里 config->mark_points_m_.begin() / .end() 是 mark_points_m_（模型点容器）的首尾迭代器，作用是把模型点拷一份成 vector<Point3d> 传给 projectPoints（它要的是 InputArray，vector 是最直接的输入形式）
            cv::projectPoints(std::vector<cv::Point3d>(config->mark_points_m_.begin(),
                                                       config->mark_points_m_.end()),
                              p.estimate_->pose_.rvec_, p.estimate_->pose_.t_m_, config->camera_.K_,
                              config->camera_.dist_, pixels);
            for (int i = 0; i < 4; ++i)
                cv::line(out, pixels[i], pixels[(i + 1) % 4], {255, 255, 0}, 1, cv::LINE_AA);
        }

        // 画跟踪（橙色）：每条轨迹写一行字（track id、状态、位置、速度），再把滤波位置投影到图像上画圆，速度画成箭头
        for (const auto &t : tracks_)
        {
            // 组标签 + 写字：
            // std::ostringstream 是个字符串流（往里面 << 就拼字符串，最后 .str() 取出来）；拼出来的字是这种格式：track 1 MEASURED p(m)=[0.100, 0.200, 1.500] v(m/s)=[0.500, 0.000, 0.000]（fixed + setprecision(3) 让数字都是 3 位小数）
            // OpenCV 给 cv::Vec 重载了 operator<<，打印格式就是 [x, y, z]
            /* std::ostringstream：
            std::ostringstream label;   // 需要 #include <sstream>
            label << ...;               // 往里面写，和 cout 一个用法
            label.str();                // 取出拼好的字符串
            输出字符串流——往里面 << 什么，它就拼成一个字符串，最后 .str() 取出来
            这里是拿来拼那行跟踪标签（track id、状态、位置、速度），拼完了一次性交给 text() 写到图上
            */
           // std::fixed + std::setprecision(3)：label << std::fixed << std::setprecision(3);
           // 两个流操纵符（也在 <iomanip> 里），管后面所有浮点数怎么写：
           // std::fixed：定点表示（不写就是默认格式，小数多了会变科学计数法）
           // std::setprecision(3)：保留 3 位小数
           // 合起来：位置、速度都显示成 0.100、1.500 这种 3 位小数的样子。这两个是"粘性"的——一旦设上，这条流后面所有浮点输出都按这个格式，直到改掉
            std::ostringstream label;
            label << "track " << t.track_id_
                  << (t.state_ == tracking::TrackState::Coasting ? " COAST" : " MEASURED") // 状态是三元运算符：COASTING 就写 " COAST"，否则 " MEASURED"
                  << std::fixed << std::setprecision(3); // std::fixed << std::setprecision(3)：定点表示、保留 3 位小数（所以位置显示成 0.100 这种，不会是科学计数法）
            label << " p(m)=" << t.position_m_;
            if (t.velocity_mps_) // 速度是 optional，有才写（未确认的轨迹没速度，这一项就不出现）
                label << " v(m/s)=" << *t.velocity_mps_;
            text(label.str(), row++, {0, 180, 255}); // 橙色 {0, 180, 255}（BGR：蓝 0、绿 180、红 255），row++ 继续往下排

            // 画点画箭头的条件：没 config（拿不到内参）或者位置的 z（深度）≤ 0（在相机后面，投不出来）就跳过
            // 轨迹均为相机坐标；只有可投影位置才画点/速度箭头。
            if (!config || t.position_m_[2] <= 0)
                continue;

            // 算投影点：
            const auto &p = t.position_m_; // MARK 坐标系原点在相机坐标系下的位置，而 MARK 坐标系的原点就是模型点 (0,0)——也就是 MARK 的左上角（+x 沿上边，+y 沿左边）
            std::vector<cv::Point3d> points{{p[0], p[1], p[2]}}; // points 先装一个点：滤波位置本身
            if (t.velocity_mps_) // 有速度就再算一个点：end = p + v * 0.1——按当前速度往前推 100ms 的位置
            {
                const auto end = p + *t.velocity_mps_ * .1; // 箭头表示100ms表观位移，仅作显示。（不是预测，就是个可视化长度）
                if (end[2] > 0) // end[2] > 0 才加进来（箭头尖端也得在相机前面）
                    points.push_back({end[0], end[1], end[2]});
            }

            // 投影 + 画：
            std::vector<cv::Point2d> pixels;
            // projectPoints：这里 rvec 和 tvec 都传 cv::Vec3d()（零向量 = 单位旋转、零平移），因为 points 本来就是相机坐标系下的点，不需要再变换——直接按内参 K 和畸变 dist 投到图像上
            // cv::Vec3d()：默认构造，全 0，即 (0, 0, 0)；这里 projectPoints 的 rvec 和 tvec 都传零向量，意思是"不旋转、不平移"——因为 points 本来就是相机坐标系下的点，直接按内参投影就行
            // cv::projectPoints 是逐点投影：输入几个点，输出就是几个点，顺序不变；pixels[0] 对应 points[0]（滤波位置），pixels[1] 对应 points[1]（速度箭头尖端）
            cv::projectPoints(points, cv::Vec3d(), cv::Vec3d(), config->camera_.K_,
                              config->camera_.dist_, pixels);
            // checkRange：投影出来的像素点必须有限（不是 NaN/Inf），否则跳过
            if (!cv::checkRange(cv::Mat(pixels)))
                continue;
            // cv::circle：在投影点画个圆，半径 5，橙色，粗细 2（描边，不是实心）
            cv::circle(out, pixels[0], 5, {0, 180, 255}, 2);
            // pixels.size() == 2（有速度、箭头尖端也投影成功了）才画箭头：cv::arrowedLine 从位置点指向速度箭头尖端
            // cv::arrowedLine：void cv::arrowedLine(InputOutputArray img, Point pt1, Point pt2, const Scalar& color, int thickness = 1, int lineType = LINE_8, int shift = 0, double tipLength = 0.1);
            // 画带箭头的线
            /* 参数：
            img = out，pt1 = pixels[0]（起点：滤波位置），pt2 = pixels[1]（终点：速度箭头尖端）。
            color = {0, 180, 255}：橙色。
            thickness = 2，lineType = LINE_AA。
            shift 没传，默认 0；tipLength 没传，默认 0.1——箭头尖的长度是线段长度的 10%
            */
            if (pixels.size() == 2)
                cv::arrowedLine(out, pixels[0], pixels[1], {0, 180, 255}, 2, cv::LINE_AA);
            // ！！！可以用来调参：
            // （动态调参）拍一段 MARK 动起来的视频（最好带掉头/往返，0.5 m/s 那种），跑 pipeline 出 overlay，看橙色圆点跟得好不好：
            // q 太小：滤波器"倔"，MARK 掉头、变速的时候橙点跟不上，明显滞后在绿框后面；q 太大：滤波器太信量测，橙点跟着绿框一起抖，把检测噪声也跟进来了；q 合适：橙点贴着 MARK 平滑地走，不滞后、也不抖
            // 或者如果要定量：NIS（3 维量测，均值应该在 3 附近，太大说明模型跟不上，太小说明太信量测），在预测步骤暴露了滤波器的模型到底行不行
            // NIS 与 P：定量调参 P 是滤波器自己报的"我有多确定"，NIS 是外部检验"它报的准不准"（拿预测去撞一个独立的量测：实际偏了多少，和自己以为会偏多少）
            //
            // NIS = residual^T * S^-1 * residual，其中 residual = 量测 - 预测位置（x-），
            // S = H*P-*H^T + R。是“预测准不准”的检验，不是“输出平滑不平滑”的检验。
            //   - 算的是 correct 之前（预测 vs 量测），不是 correct 之后。后验已经吃了量测，
            //     再量它和量测的距离永远显得很小，测不出模型的毛病；只有预测是诚实的。
            //   - 白化后 NIS = y1^2 + y2^2 + y3^2，y ~ N(0, I3)，所以 NIS ~ χ²₃，均值 3
            //     （3 维残差，每维平均贡献 1；99% 分位点 11.34）。
            //   - NIS 长期 >> 3：滤波器过度自信，实际误差比它以为的大。典型原因 Q 太小
            //     （没料到目标会转向），P- 缩得太小，预测老是偏。
            //   - NIS 长期 << 3：滤波器过于胆小，实际误差比它以为的小。典型原因 Q 或 R 太大。
            //   - NIS ≈ 3：模型和现实对得上，Q、R 标得合适。
            //
            // P vs NIS 的分工：
            //   - 调参（选 Q、R）看 NIS：它是唯一能检验模型标没标对的量，因为它不依赖
            //     滤波器自己的说法。P 是滤波器自己报的“我有多确定”，模型错了它照样报个小 P，
            //     不会自己揭发自己，所以 P 不能用来调参。
            //   - 运行时（状态机判断 用不用这条轨迹）看 P：那是滤波器当下最好的不确定性估计，
            //     前提是 NIS 已经验证过这个 P 是诚实的。
            //
            // 调 q 的实操：拍一段 MARK 动起来的视频（带掉头/往返），看 overlay 里橙点跟得好不好，
            // 再看 tracks.yaml 里 NIS 的均值。q 太小：掉头时橙点滞后，NIS 偏大；q 太大：橙点跟着
            // 量测噪声抖，NIS 偏小；q 合适：橙点平滑跟随，NIS 均值 ≈ 3。
        }
        // 返回渲染好的调试帧
        return out;
    }

}
