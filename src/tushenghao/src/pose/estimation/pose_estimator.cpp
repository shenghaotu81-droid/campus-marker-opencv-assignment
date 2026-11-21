// 从同帧像素四角估计米制平面位姿，向跟踪输出名义平移及多解位置分歧。
#include "vision/pose/pose_estimator.hpp"
#include <vector>
#include "validation/pose_geometry.hpp"
#include <algorithm>
#include <opencv2/calib3d.hpp>

namespace mark::pose
{
    // 构造函数：
    PoseEstimator::PoseEstimator(PoseConfig c) : config_(std::move(c)) // 按值传参c（调用方传左值就拷一份进来，传右值就直接移进来） + std::move再把它移进成员 config_
    {
        // clone() 深拷贝 K 和畸变系数
        // cv::Mat 是引用计数的浅拷贝——拷 PoseConfig 的时候，K_ 和 dist_ 只拷了个头，数据还跟调用方那份 pose_config 共用同一块内存
        // clone() 是重新开一块内存把数据实打实拷一份，让估计器手里的标定数据独立出来（防止调用方之后改了自己的 K_，估计器用的标定发生改变，静默错误）
        // 只克隆这两个，是因为 PoseConfig 里只有 K_ 和 dist_ 是 Mat（引用类型）；模型点那些是 Point3d 数组，值类型，拷配置的时候已经是独立的了，不用再克隆
        // cv::Mat::clone()：返回一个深拷贝：新开一块内存，把数据实打实拷过去，新 Mat 自带独立的 buffer
        // 对比：Mat b = a;——浅拷贝，只拷头，b 和 a 共用同一块数据，改一个另一个也变；a.copyTo(b);——把 a 的数据拷进 b（b 尺寸类型不对会重开），拷完也是独立的，但 copyTo 是“往现成的 b 里拷”，clone 是“新造一个带数据的 Mat 返回”
        // 要独立的拷贝用 clone（或 copyTo），共享数据用 =
        config_.camera_.K_ = config_.camera_.K_.clone();
        config_.camera_.dist_ = config_.camera_.dist_.clone();
    }

    // MARK 位姿估计：把检测器的 FrameResult（检到的角点）变成 PoseObservation（名义位姿 + S + 失败原因）
    PoseObservation PoseEstimator::estimate_mark(const detector::FrameResult &raw, size_t index,
                                                 cv::Size size, const CaptureStamp &stamp) const
    {
        // 填身份：把 stamp 拷进观测，source_ 写 "MARK"，target_id_从配置取，detection_index_ 记下是第几个检测
        PoseObservation out;
        out.stamp_ = stamp;
        out.source_ = "MARK";
        out.detection_index_ = index;

        // 验状态：raw.status_ 必须是 DETECTED；NOT_DETECTED 直接返回（连原因都不写，就是“这帧没检到”）；其他状态 → DETECTION_INPUT_INVALID
        if (raw.detections_.empty())
            return out;

        const auto &d = raw.detections_[index];
        // 重排角点：按 orientation_ 把屏幕顺序的四个角点排成物理顺序，得到 pixels
        // pixels[i] 是模型点 mark_points_m_[i] 在图里的位置，两个数组是按同一个顺序对齐的：
        // pixels[0] ↔ 模型点 (0,0)——左上
        // pixels[1] ↔ 模型点 (80,0)——右上
        // pixels[2] ↔ 模型点 (80,80)——右下
        // pixels[3] ↔ 模型点 (0,80)——左下
        std::array<cv::Point2d, 4> pixels;
        for (size_t i = 0; i < 4; ++i)
            pixels[i] = d.corners_[d.orientation_[i]];
        
        // 核心
        // 调 estimate_planar：把模型点（80 mm 方块，米制）、重排后的像素角点、图尺寸、stamp、target id 传进去，真正算 IPPE PnP + S 的地方
        out = estimate_planar(config_.mark_points_m_, pixels, size, stamp);

        // 补身份：estimate_planar 返回的观测 source_ 是 "PLANAR"，改回 "MARK"，再把 detection_index_ 补上
        out.source_ = "MARK";
        out.detection_index_ = index;
        // 其他字段（stamp_、target_id_、estimate_、reason_）都已经在 estimate_planar 里填好了
        // 返回结果
        return out;
    }

    // estimate_planar 是位姿估计的核心：
    // 输入 4 个米制模型点 + 4 个像素角点，用 IPPE 平面 PnP 解出最多两个位姿候选，逐个验（重投影误差 + 深度），RMSE 最小的当名义位姿，另一个解跟它的平移分歧按 S 方法压成一个 3×3 的协方差增量（translation_spread_m2_），交给跟踪器加进量测噪声
    // 输入或模型点验不过、PnP 无解，就带原因返回，没有位姿
    PoseObservation PoseEstimator::estimate_planar(const std::array<cv::Point3d, 4> &object,
                                                   const std::array<cv::Point2d, 4> &image_,
                                                   cv::Size size, const CaptureStamp &stamp) const
    {
        // 填身份
        // stamp 拷进观测，source_ 写 "PLANAR"（注意这里不是 "MARK"，是 estimate_mark 回去之后再改回来的），target_id_ 取传进来的 id
        PoseObservation out;
        out.stamp_ = stamp;
        out.source_ = "PLANAR";

        // 验输入
        // 四个条件任一不满足就 STAMP_CAMERA_OR_QUAD_INVALID：id 为空、stamp 的相机跟配置对不上、图尺寸跟配置对不上、像素四角不合法（valid_quad（角点有限、在图内、严格凸））
        if (size != config_.camera_.image_size_ || !detail::valid_quad(image_, size))
        {
            out.reason_ = "STAMP_CAMERA_OR_QUAD_INVALID";
            return out;
        }

        // 验模型点
        // 四个模型点逐个查：x、y 必须有限（不能是 NaN/inf），z 必须等于 0（MARK 是平面板，模型点都在 z=0 平面上，IPPE 是平面 PnP）
        // 不满足 → MODEL_INVALID
        for (size_t i = 0; i < 4; ++i)
        {
            auto p = object[i];
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.z != 0)
            {
                out.reason_ = "MODEL_INVALID";
                return out;
            }
        }

        // IPPE 求解 + 候选评估
        // 调 solvePnPGeneric（IPPE 模式，最多给两个解），每个解抽出 rvec/tvec 拼成 RigidPose，再过 evaluate_pose（重投影 RMSE + 深度复核），合法的才进 candidates
        // 重要理解：
        // MARK 坐标系（模型系）：原点是模型点 (0,0)（左上角），+x 沿上边指向右上角，+y 沿左边指向左下角，+z 由右手定则 x × y 决定。单位米，四个模型点都在 z=0 平面上。
        // 相机坐标系（OpenCV 标准）：原点是光心，+x 向右（图像右），+y 向下（图像下），+z 沿光轴向前（进场景）
        // 注释：光心不是主点 (cx, cy)（是光心在像平面z = f上的投影（像素坐标），是个 2D 点，不是 3D 原点）；也不是图像中心（图的一半，跟主点不是一回事，主点相对图像中心偏离）
        //       光心是针孔模型里的投影中心，不是镜头上某个看得见的点（模型里的数学点）
        // 位姿（rvec/tvec）就是 MARK 系 → 相机系的变换：p_相机 = R · p_MARK + t，R 由 rvec 转来，t 就是 tvec。tvec 是 MARK 原点（左上角）在相机系里的位置，z 分量就是深度
        // 大致流程：！！！
        // 像素点 → 光心系：传进去的像素点用的是像素坐标系，原点在图像左上角 (0,0)，x 向右、y 向下；先K（逆变换）换坐标，把像素点换成带畸变的归一化坐标（归一化坐标：cx、cy 把原点移到主点，fx、fy 把像素换成焦距倍数），dist 再去畸变，在归一化平面（以光心为基准的平面）上迭代补偿（把镜头扭过的点掰直），得到从光心出发穿过像素点对应的归一化点 (x, y, 1)（像平面在 z=1 处）的射线方向（先 K⁻¹（像素 → 带畸变的归一化坐标），再 dist（去畸变 → 真归一化坐标））
        // PnP 求解：拿着模型点（MARK 系）和这些射线（相机系），解出 R、t，让模型点变换后正好落在射线上，输出 rvec/tvec
        // 四个像素角点经 K⁻¹ 和去畸变，变成归一化平面（z=1）上的四个点 (xᵢ, yᵢ, 1)。从光心 (0,0,0) 出发、指向这四个点的四条射线，方向就是 (xᵢ, yᵢ, 1)；PnP 要做的就是：找到 R、t，让四个模型点变换到相机系之后，正好分别落在这四条射线上
        // 合法像素四角与米制模型送入平面 PnP，候选由重投影和深度复核。
        std::vector<detail::PoseCandidate> candidates; // 空向量，收后面验合格的位姿候选
        // 整个求解包在 try 里——solvePnPGeneric 数值失败时会抛 cv::Exception，抛了就进 catch
        // try / catch (const cv::Exception &)（C++ + OpenCV）：cv::Exception 是 OpenCV 的异常类型，按 const 引用捕获
        try
        {
            std::vector<cv::Mat> rotations, translations; // 长度是 0、1 或 2（正常情况是 2）
            // solvePnPGeneric：4 个模型点 + 4 个像素点 + 内参 K + 畸变，false 是不用初值从头算，SOLVEPNP_IPPE 是平面专用算法
            // IPPE 对平面目标最多给两个解，分别存进 rotations、translations（每个解一个 3×1 的 rvec 和一个 3×1 的 tvec）
            // cv::solvePnPGeneric（OpenCV）：多解版的 solvePnP
            // 完整签名：int cv::solvePnPGeneric( 
            // InputArray objectPoints,
            // InputArray imagePoints,
            // InputArray cameraMatrix,
            // InputArray distCoeffs,
            // OutputArrayOfArrays rvecs,
            // OutputArrayOfArrays tvecs,
            // bool useExtrinsicGuess = false,
            // SolvePnPMethod flags = SOLVEPNP_ITERATIVE,
            // InputArray rvec = noArray(),
            // InputArray tvec = noArray(),
            // OutputArray reprojectionError = noArray());
            // 输入是模型点、像素点、内参3×3、畸变系数；输出是 rvecs、tvecs 两个向量数组（每个元素一个 3行×1列 的 Mat）
            // rvec 是 Rodrigues 向量（轴角表示）：方向是旋转轴，长度是转角（弧度）。它是一个绕单根轴转一次的表示。（不是欧拉角）
            // tvec：平移向量，单位是米。[x, y, z]
            // flags 选算法——SOLVEPNP_IPPE 专用于平面目标，给出两个歧义解；SOLVEPNP_ITERATIVE 是通用迭代法，只给一个解；useExtrinsicGuess 为 true 时用传入的初值接着算
            // 初值 rvec/tvec（默认 noArray()），只有 useExtrinsicGuess=true才用得上
            // reprojectionError（默认 noArray()），输出：每个解的重投影误差。这里不要，因为代码自己用 evaluate_pose 算 RMSE（自己定口径，独立复核，顺便深度检查（点必须在相机前方））
            // 返回值是 int——解的个数（IPPE 对平面目标返回 0、1 或 2）
            // InputArray 是 OpenCV 的通用输入包装，能接 Mat、vector 等多种类型；OutputArrayOfArrays 是“输出一组 Mat”的包装，这里接的是 vector<Mat>；noArray() 就是个“空数组”的哨兵，表示这个可选参数没给
            cv::solvePnPGeneric(std::vector<cv::Point3d>(object.begin(), object.end()), // std::vector<T>(begin, end)（std）：范围构造，从一对迭代器拷一份；object 是 std::array，solvePnPGeneric 只要 vector，就 std::vector<cv::Point3d>(object.begin(), object.end()) 拷 4 个点过去
                                std::vector<cv::Point2d>(image_.begin(), image_.end()), // 把定长数组喂给只要 vector 的接口时就这么写
                                config_.camera_.K_, config_.camera_.dist_, rotations, translations,
                                false, cv::SOLVEPNP_IPPE);
            // 循环每个解
            for (size_t i = 0; i < rotations.size(); ++i)
            {
                RigidPose p;
                // 适配器：
                // 内层 k = 0..2 把 Mat 里的 3×1 向量逐个元素拷进 RigidPose的 rvec_ / t_m_（ Vec3d）
                // 搬之前：rotations[i] 是个 cv::Mat，3×1 的列向量，数据在堆上，引用计数，OpenCV 的通用矩阵；
                // 搬之后：p.rvec_ 是个 cv::Vec3d（RigidPose 的定义，pose_types.hpp里：cv::Vec3d rvec_{0,0,0}, t_m_{0,0,0};），3 个 double 直接存在对象里，值类型（Vec3d定长三维向量，值语义，算叉积、点积、外积（S 里的 delta * delta.t()）都顺手，也不用操心 Mat 的行列和共享）
                for (int k = 0; k < 3; ++k)
                {
                    // cv::Mat::at<double>(k)（OpenCV）：取单列（或单行）Mat 的第 k 个 double 元素
                    // .at<T> 是带边界检查的元素访问；二维的还有 .at<T>(row, col)
                    p.rvec_[k] = rotations[i].at<double>(k); // rotations[i] 是个 3×1 的 double 矩阵，.at<double>(0) 是 rvec 的 x 分量，(1) 是 y，(2) 是 z
                    p.t_m_[k] = translations[i].at<double>(k);
                }
                // 然后 evaluate_pose 独立复核这个解——把 3D 模型点按这个位姿重投影回图像，跟观测的 2D 点比，算 RMSE，再查深度——合法才 push_back 进 candidates
                auto c = detail::evaluate_pose(p, object, image_, config_.camera_.K_,
                                               config_.camera_.dist_);
                if (c)
                    candidates.push_back(*c);
            }
        }
        catch (const cv::Exception &)
        {
            // OpenCV 抛异常 → PNP_NUMERICAL_FAILURE
            out.reason_ = "PNP_NUMERICAL_FAILURE";
            candidates.clear(); // 清空
        }
        // 空候选处理
        // 一个合法解都没有（要么没解出来，要么全被 evaluate_pose 拒了，要么抛了异常）。reason_ 还是空的（即不是异常那条路）就写 PNP_NO_LEGAL_SOLUTION，返回（没有位姿）
        if (candidates.empty())
        {
            // 一个合法的都没有 → PNP_NO_LEGAL_SOLUTION
            if (out.reason_.empty())
                out.reason_ = "PNP_NO_LEGAL_SOLUTION";
            return out;
        }

        // 选最优 + 算 S
        // RMSE 最小的当名义位姿；
        // 其余每个候选按 S 方法算权重，把平移差的外积加权累加进 translation_spread_m2_（S 的本体）
        // 选最优（定名义（RMSE 最小者））
        // std::min_element + lambda 比较器，按 rmse_px_ 找最小的那个候选
        // 这就是名义位姿：RMSE 最小的解当作“名义测量”，其余的只用来算分歧
        // std::min_element 完整签名：template<class ForwardIt, class Compare> ForwardIt min_element(ForwardIt first, ForwardIt last, Compare comp);
        // first = candidates.begin()：范围起点；last = candidates.end()：范围终点（不包含）；comp = 那个 lambda：比较器，a 比 b 小时返回 true。这里比的是 rmse_px_，谁 RMSE 小谁就是“更小”
        // 作用：在 [first, last) 里找最小的元素，返回指向它的迭代器（位置），不是值本身（所以后面用的是 best->pose_、best->rmse_px_（箭头穿过迭代器），循环里用 it == best 比较迭代器）
        // 特点：1.返回迭代器：这是它和 std::min（返回两个值中较小的那个值）的区别 2. 并列时返回第一个：如果多个元素同样最小，返回最靠前的那个 3.空范围返回 last：如果 candidates 是空的，返回 candidates.end() 4.复杂度：线性，一遍扫完，O(n) 次比较 5.lambda 是泛型 lambda（const auto & 参数，C++14）：类型由元素类型推导，这里就是 const PoseCandidate &
        const auto best = std::min_element(candidates.begin(), candidates.end(),
                                           [](const auto &a, const auto &b)
                                           {
                                               return a.rmse_px_ < b.rmse_px_;
                                           });
        // 填 estimate
        out.estimate_.emplace(); // 在 optional 里原地构造出 PoseEstimate（全零）
        out.estimate_->pose_ = best->pose_; // 把 best 的位姿和 RMSE 拷进去；第一个->等价于(*out.estimate_).pose_，pose_ 不是 optional 的成员，是 optional 里面装着的 PoseEstimate 的成员，第二个->是因为best 是迭代器，pose_ 不是迭代器自己的成员，是它指向的那个 PoseCandidate 的成员
        out.estimate_->rmse_px_ = best->rmse_px_; // translation_spread_m2_ 初始是零（PoseEstimate里默认 Matx33d::zeros()），等下一步累加

        // S 累积循环（量分歧（其余解与名义的加权平移外积，累进 S））；是否正确，有待商榷（两个 IPPE 解在位置上差多少，换算成一块加到卡尔曼量测噪声 R 上的不确定性）
        // 连续残差权重将求解器内部多解压缩为位置二阶矩，交给跟踪量测噪声。
        // 对每个不是 best 的候选（IPPE 最多 2 个解，所以这个循环最多跑一次；只有 1 个候选时一次都不跑，S 保持零）：
        for (auto it = candidates.begin(); it != candidates.end(); ++it)
        {
            if (it == best) // 跳过 best 本身，用位置（迭代器）来比对
                continue;
            const double scale = std::max(best->rmse_px_, it->rmse_px_); // 用两者中较大的 RMSE 做归一化分母
            // a = best_rmse / scale，b = it_rmse / scale：归一化后的误差，都在 [0,1] 里（scale 为 0 时都取 1，防除零）
            const double a = scale > 0 ? best->rmse_px_ / scale : 1; // 在 (0, 1] 之间
            const double b = scale > 0 ? it->rmse_px_ / scale : 1; // 1
            // ！！权重。注意 a 是 best 的误差、b 是对方的误差。
            // best 明显更好时（a 远小于 b），weight 趋近 0——对方不太可能是真的，分歧不用太当真；两者差不多时（a ≈ b），weight 趋近 0.5——最含糊，分歧权重最大
            const double weight = a * a / (a * a + b * b);
            /* 可以换似然权重（更锐利）：w ∝ exp(-n·e² / 2σ²)，SSR = n·RMSE²； e_i 是第 i 个解的 RMSE，σ 是像素噪声（“MARK 角点检测的单坐标像素噪声”）的标准差（单位 px）；两个解的权重归一化之后，就是各自是真解的后验概率
            概率论？（高斯噪声假设下的贝叶斯后验）
            预期改动代码：
            // n = 4 个角点；e 是每点二维距离的 RMS（我们的 rmse 定义）；sigma 是单坐标像素噪声
            // 注意：sigma 的值待定。候选 0.095px（标定拟合残差）vs 0.36px（作业视频实测 MARK 录像 RMSE 中位数），
            // 两者都不是 sigma 本身，需 板上实测后填。
            // sigma 测量主方法（板上）：相机与 MARK 都固定不动，连采 N 帧，（静态测：相机和 MARK 都不动，看角点在多帧里的抖动，测的就是检测噪声本身）
            // 对每个角点取其像素坐标在 N 帧里的标准差，4 角点 × 2 坐标汇总即 sigma。
            // 备选校验：跑起来时同步记每帧每角点的重投影残差 dx、dy，汇总其标准差对照。（跑着测：贴近实战，但残差里混了位姿误差，所以测出来的是 σ 的上界，适合拿来跟静态结果对照）
            // 注意：这个指数只对我们的 RMSE 定义成立（每点二维距离的 RMS，除以 n）。
            // OpenCV 的 reprojectionError 除以 2n，对应的是 exp(-4e²/σ²)——别把那个抄过来。
                const double n = 4.0;
                const double w0 = std::exp(-n * best_rmse * best_rmse / (2 * sigma * sigma));
                const double w1 = std::exp(-n * it_rmse * it_rmse / (2 * sigma * sigma));
                const double weight = w1 / (w0 + w1);
            // 状态：待 对比实验裁决（固定 R / 逆平方 S / 似然 S / PDAF / 去掉 S 五选一）。
            */
            // 对方平移与 best 平移的差（Vec3d）
            const auto delta = it->pose_.t_m_ - best->pose_.t_m_;
            // 把加权的外积（Δt·Δtᵀ，一个 3×3 矩阵）累进 S！！
            // delta * delta.t()：外积
            // delta 是 3×1 列向量，delta.t() 是转置后的 1×3 行向量，相乘得到 3×3 矩阵，第 (i, j) 项就是 delta[i] * delta[j]
            /* ！！特殊的协方差矩阵：外积 只沿一个方向有不确定性——全部方差都集中在 delta 这一根线上，垂直方向全是 0（不确定性只沿 delta 一个方向，别的方向是 0）
            [dx²   dx·dy  dx·dz]
            [dy·dx dy²    dy·dz]     对角线是各分量的平方（各轴的方差），非对角线是交叉项（轴与轴的相关性）->沿某个单位方向 u = (ux, uy, uz) 的方差，是把对称矩阵 M 的所有项按 u 的分量加权求和：
            [dz·dx dz·dy  dz²  ]                                                                       Var = ux²·Mxx + uy²·Myy + uz²·Mzz+ 2·ux·uy·Mxy + 2·ux·uz·Mxz + 2·uy·uz·Myz
                                                                                                外积情形   = (ux·dx + uy·dy + uz·dz)²= (u·delta)²
            */
            // S 是协方差矩阵，要加到同样是 3×3 矩阵的 R 上（R_eff = R_profile + S），所以它本身也得是 3×3
            // 协方差矩阵（对称，沿任何方向算出的方差都 ≥ 0）：描述一个向量（比如三维位置）的不确定性 ；对角线：各分量的方差（不确定性大小）；非对角线：各分量之间的相关性（不确定性方向）
            // 对角线：每个分量自己的方差。Var(x)、Var(y)、Var(z)，就是各轴上的 spread（方差 = 标准差的平方）。值越大，那个方向越不确定。
            // 非对角线：两个分量之间的协方差。Cov(x, y) > 0 表示 x 和 y 倾向于一起变大变小；等于 0 表示两者独立变化
            // 几何上，一个协方差矩阵就是一个不确定性椭球：估计值大概落在这个椭球里，椭球沿某个方向拉得越长，那个方向越不确定（对角矩阵的椭球轴和坐标轴对齐；非对角线不为零时，椭球就转了个角度）
            // 放到卡尔曼里：R 是量测噪声的协方差（量测在各方向上多不可靠），P 是状态估计的协方差，S 也是一个协方差——两个解分歧带来的额外不确定性，所以它必须是个 3×3 矩阵才能加到 R 上
            // S 的构造是：分歧向量 → 外积 → 协方差矩阵 → 加到 R 上
            // 点积（内积）是两个向量乘出一个数；外积是两个向量乘出一个矩阵；叉积是乘出一个向量
            // 这里用外积，外积就是把一个向量变成矩阵的办法，delta * delta.t() 恰好就是“两个可能位置相差 delta”的那份不确定性的协方差
            // 真实位置要么是 best.t，要么是 it.t，两者差一个 delta，未知是哪一个——这个“二选一”的不确定性，写成协方差矩阵就是 delta 的外积（再乘个权重表示另一个解有多可信）
            // 外积的几何意义：delta * delta.t() 表示的不确定性全部沿着 delta 的方向，垂直于 delta 的方向上一点没有。它的不确定性椭球退化成了一根沿 delta 的线段
            // 联系R：R 本身是个小椭球（对角矩阵，三个轴分别是 x、y、z 的标准差）。R + S 就是把这个小椭球沿着 delta 的方向拉长——S 那根线段有多长，R 的椭球就沿那个方向被撑多长，即沿着两个解分歧的方向，把不确定性撑大
            // ！！外积的几何意义就是一根线——两个解之间的那根连线，方向是 delta，长度是 |delta|，它说“不确定性只在这根线上，别处没有”
            // 协方差矩阵一定对称Cov(X, Y) = Cov(Y, X)；单个外积是很特殊的一种对称矩阵：不确定性只沿一个方向（就是 delta 那根线）；一般的协方差矩阵可以在多个方向上都有不确定性
            // ！！协方差矩阵就是“大小 + 朝向”：对角项给大小（Mxx、Myy、Mzz，各轴自己的方差（各个方向多不确定），决定椭球沿每根坐标轴伸多远），非对角项给朝向（Mxy、Mxz、Myz，两轴之间的协方差（一个轴偏了，另一个轴跟着偏多少），决定椭球往哪个方向倾斜；走到极端（外积），非对角项正好大到把椭球压扁成一根线——所有不确定性都被“搬”到了 delta 方向上，垂直方向归零）
            // ！！一般的协方差矩阵可以是“饱满”的椭球（多个方向都有不确定性），外积是退化成一根线的极端情况。S 就是这根线，R 是饱满的椭球，两者相加就是把 R 的椭球沿 delta 方向拉长。
            // weight * (...)：按可信度缩放；+= 是累加进 translation_spread_m2_（初值是零矩阵；IPPE 最多 2 个解，这循环最多跑一次）
            out.estimate_->translation_spread_m2_ += weight * (delta * delta.t());
        }
        return out;
    }

}
