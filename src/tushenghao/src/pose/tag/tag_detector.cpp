// 解码白名单 Tag 并保持像素角身份；有实际边长时再交给平面位姿估计。
#include "vision/pose/tag_detector.hpp"
#include "validation/pose_geometry.hpp"
#include <opencv2/imgproc.hpp>
#include <apriltag/apriltag.h>
#include <apriltag/tag36h11.h>
#include <map>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace mark::pose
{
    // pimpl
    // Impl
    struct TagDetector::Impl
    {
        TagConfig config_; // TAG 的配置（白名单 id、边长）
        // family_ 和 detector_ 是带自定义删除器的 unique_ptr
        // C 库的对象（AprilTag 是 C 库），不能用 C++ 的 delete 释放，必须用对应的 C 销毁函数（tag36h11_destroy / apriltag_detector_destroy）
        // apriltag_family_t *tag36h11_create();无参，返回新 family 指针，失败返回 NULL
        // void tag36h11_destroy(apriltag_family_t *tf);释放 family 
        // 写法：把销毁函数当删除器传进去——unique_ptr 析构时自动调 C 函数释放，C 对象也能享受 RAII
        // family_：AprilTag 的码族，tag36h11_create() 建出来的——退出 TAG 是 TAG36H11 家族
        // std::unique_ptr 带自定义删除器：template<class T, class Deleter = std::default_delete<T>> class unique_ptr; 用的构造函数：unique_ptr(pointer p, const Deleter& d);（{} 是列表初始化，{指针, 删除器} 调用）
        // T = apriltag_family_t（管的对象类型）；Deleter = decltype(&tag36h11_destroy)，也就是 void (*)(apriltag_family_t*)（一个函数指针类型）；
        // 构造参数 p = tag36h11_create() 的返回值（新分配的 family 指针）；构造参数 d = tag36h11_destroy 这个函数本身（退化成函数指针存起来）
        // unique_ptr 析构时调 d(p)，也就是 tag36h11_destroy(family_ptr)——C 对象自动用 C 销毁函数释放，不用手写
        // decltype(表达式)：得到表达式的类型，不求值
        // decltype(&tag36h11_destroy)：&tag36h11_destroy 是指向该函数的指针，类型是 void (*)(apriltag_family_t*)，decltype 就把这个类型拿来当 Deleter 模板参数
        std::unique_ptr<apriltag_family_t, decltype(&tag36h11_destroy)> family_{tag36h11_create(),
                                                                                tag36h11_destroy};
        // detector_：AprilTag 的检测器，apriltag_detector_create() 建出来的
        // detector_ 同上，T = apriltag_detector_t，删除器是 apriltag_detector_destroy
        // apriltag_detector_t *apriltag_detector_create();无参，返回新 detector 指针，失败返回 NULL
        // void apriltag_detector_destroy(apriltag_detector_t *td);释放 detector
        std::unique_ptr<apriltag_detector_t, decltype(&apriltag_detector_destroy)> detector_{
            apriltag_detector_create(), apriltag_detector_destroy};

        // Impl 构造函数
        explicit Impl(TagConfig c) : config_(std::move(c)) // 配置移进来（by-value + move）
        {
            // 先判空：两个 C 分配有一个失败就直接抛 runtime_error("TAG_ALLOCATION_FAILED")，构造时就炸掉，不等到第一次 detect() 才发现
            if (!family_ || !detector_)
                throw std::runtime_error("TAG_ALLOCATION_FAILED"); // explicit runtime_error(const std::string& what_arg);字符串字面量转成 string 存进异常对象，what() 返回它
            // 显式配检测器参数（apriltag_detector_t 结构体的字段，管以后每次 detect() 时的行为）
            detector_->nthreads = 1; // 单线程
            detector_->quad_decimate = 1; // 不降采样，全分辨率找四边形（2.0 是把图像先缩小一半再找（快，但粗））
            detector_->quad_sigma = 0; // 不模糊，保持原图锐利
            detector_->refine_edges = true; // 精修边，角点更准（亚像素级别）
            detector_->decode_sharpening = 0.25; // 解码前锐化
            // 把 tag36h11 这个码族注册到检测器上，告诉它只找这个家族的 TAG（检测器会把视野里所有 tag36h11 的 TAG 都检出来（不管 id 是多少））
            // 签名：void apriltag_detector_add_family_bits(apriltag_detector_t *td, apriltag_family_t *fam, int bits_corrected);
            // unique_ptr::get()，签名：pointer get() const noexcept; 返回存着的裸指针，不交出所有权，给 C 函数用（unique_ptr 仍然拥有它、到时负责销毁）；release() 是把所有权交出去（返回指针并把自己置空）
            // td = detector_.get()（裸 detector 指针）；fam = family_.get()（裸 family 指针）；bits_corrected = 0（纠错位数，0 = 不纠错，更严格，库推荐2）
            // 调用方仍然“拥有” family（detector 不接管，family_ 的 unique_ptr 负责销毁）；同一个 family 实例只给一个 detector 用
            apriltag_detector_add_family_bits(detector_.get(), family_.get(), 0);
        }
    };

    // 构造
    // TagDetector 构造就是 make_unique<Impl> 把配置递进去
    // std::make_unique：template<class T, class... Args> unique_ptr<T> make_unique(Args&&... args);
    // 用 std::move(c) 作参数调用 Impl 的构造函数，造一个 Impl，包进 unique_ptr<Impl>（默认删除器，因为 Impl 是 C++ 对象，delete 就行）返回，用来初始化 impl_
    TagDetector::TagDetector(TagConfig c) : impl_(std::make_unique<Impl>(std::move(c)))
    {
    }

    // 析构
    // 析构 = default 写在这里（.cpp 里），因为 unique_ptr<Impl> 要析构一个完整类型，头文件里 Impl 只是前置声明，析构函数必须放在能看到完整定义的地方
    TagDetector::~TagDetector() = default;

    // detect()：主入口，输入一张图和一个 CaptureStamp，返回这一帧里检到的所有 tag（没检到就返回空 vector）
    std::vector<TagObservation> TagDetector::detect(const cv::Mat &bgr, const CaptureStamp &stamp)
    {
        // 输入校验：图像必须是非空、8 位三通道 BGR、二维
        // bgr.empty() —— bool empty() const;：矩阵没有元素（行/列为 0 或无数据）时返回 true
        // bgr.type() —— int type() const;：返回类型码（位深 + 通道数）。CV_8UC3 就是 8 位无符号、3 通道（BGR 彩图）。类型不对就不是彩图
        // bgr.dims —— int dims;（公开成员变量）：维度数。正常图像是 2 维；dims != 2 说明不是一张普通图像
        // std::invalid_argument("TAG_INPUT_INVALID") —— explicit invalid_argument(const std::string& what_arg);：参数无效时抛的异常，what() 返回这个字符串
        if (bgr.empty() || bgr.type() != CV_8UC3 || bgr.dims != 2)
            throw std::invalid_argument("TAG_INPUT_INVALID");
        
        // 转灰度 + 步长检查
        // AprilTag 只吃灰度图
        // cv::cvtColor ——void cvtColor(InputArray src, OutputArray dst, int code, int dstCn = 0);
        // src = bgr，dst = gray（自动分配成单通道），code = cv::COLOR_BGR2GRAY（= 6，BGR 转灰度），dstCn = 0（默认，自动定）
        // 灰度值按亮度公式算：gray = 0.114·B + 0.587·G + 0.299·R
        cv::Mat gray;
        cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
        // 检查 gray.step（每行字节数）能不能塞进 int（AprilTag C 库的 image_u8_t 结构体里 step 是 int，灰度图太宽就溢出）
        // AprilTag 的图像结构里步长是 int，超大图像的行步长会溢出，先拦一道
        // gray.step —— Mat::step 不是一个数字，是个 MatStep 对象；有隐式转换 operator size_t() const;，转出来的是 buf[0]，也就是每行字节数（例如：1440 宽的灰度图，step = 1440）
        // std::numeric_limits<int>::max() —— static constexpr T max() noexcept;：返回 int 的最大值 2147483647（保证步长能塞进 int（因为下块的 image_u8_t.stride 是 int32_t），超了后面 int(gray.step)会溢出）
        if (gray.step > size_t(std::numeric_limits<int>::max()))
            throw std::invalid_argument("TAG_STRIDE_OVERFLOW");

        // 包成 AprilTag 图像，调 C 库检测
        // image_u8_t 是 AprilTag 的图像结构（image_types.h），这里只是拿 gray 的数据指针包一层视图（宽、高、步长、数据指针），不拷贝像素
        // struct image_u8{const int32_t width;    const int32_t height;    const int32_t stride;    uint8_t *buf;}
        // 聚合初始化，按顺序填：width = gray.cols（比如 1440），height = gray.rows（比如 1080），stride = int(gray.step)（每行字节数），buf = gray.data（像素数据指针）
        // 注意前三个字段是 const，只能在初始化时填，不能事后赋值；而且这里不拷贝像素，buf 只是借 gray 的数据，所以 gray 在检测期间必须活着
        image_u8_t view{gray.cols, gray.rows, int(gray.step), gray.data};
        // apriltag_detector_detect ：真正干活的 C 库函数，返回一个 zarray_t（AprilTag 的动态数组），里面是一堆 apriltag_detection_t*
        // apriltag_detector_detect ——zarray_t *apriltag_detector_detect(apriltag_detector_t *td, image_u8_t *im_orig);
        // 映射：td = impl_->detector_.get()（裸检测器指针），im_orig = &view（上面那个图像结构）；
        // 返回 zarray_t*（AprilTag 的动态数组，里面是一堆 apriltag_detection_t*），失败返回 NULL
        // ！！说明：
        // 前面（Impl 构造）：apriltag_detector_create() 创建检测器对象，再配参数、注册 tag36h11 家族。这是造工具，只跑一次。产物就是 detector_ 那个 apriltag_detector_t 对象，里面装着配置（单线程、全分辨率等）和已注册的家族
        // 这里（detect）：apriltag_detector_detect(detector, image)是用工具（“让前面那个检测器干活”的函数），每帧跑一次。它拿前面造好的那个检测器对象（第一个参数 impl_->detector_.get()）+ 当前帧图像，返回检测结果
        // C 库的命名是 apriltag_detector_<动作>：create（创建）、detect（检测）、destroy（销毁）、add_family（加家族），是针对同一个检测器对象的操作
        std::unique_ptr<zarray_t, decltype(&apriltag_detections_destroy)> ds(        // 检测结果数组由 unique_ptr 管着，函数返回时自动销毁
            apriltag_detector_detect(impl_->detector_.get(), &view), apriltag_detections_destroy); // 跟前面 family_ / detector_ 同一个套路：T = zarray_t，删除器是 apriltag_detections_destroy（签名 void apriltag_detections_destroy(zarray_t *detections);）；C 库对象 + 对应的 C 销毁函数，就得用自定义删除器的 unique_ptr 包起来，让它在作用域结束时自动调 apriltag_detections_destroy 把整个检测结果（数组 + 里面每个检测）一次释放掉（不能用 C++ 的 delete——delete会调错释放器（C 分配的内存用 C++ 的方式释放是未定义行为））
        // 结果同样用带自定义删除器的 unique_ptr 包起来（apriltag_detections_destroy），返回空指针就抛 TAG_BACKEND_FAILURE
        if (!ds)
            throw std::runtime_error("TAG_BACKEND_FAILURE");  // 返回空指针说明 C 库内部失败，抛异常
        
        // 逐个过滤、转成 TagObservation
        std::vector<TagObservation> out;
        std::map<int, int> counts;
        // 循环取每个检测结果
        // zarray_size（AprilTag C 库）：static inline int zarray_size(const zarray_t *za); 返回数组里的元素个数
        // 映射：zarray_size(ds.get())——ds.get() 是裸 zarray_t*，返回这一帧检到的 TAG 数量（比如检到 3 个就返回 3），就是 for循环的上界。
        for (int i = 0; i < zarray_size(ds.get()); ++i)
        {
            apriltag_detection_t *d = nullptr;
            // zarray_get（AprilTag C 库）：static inline void zarray_get(const zarray_t *za, int idx, void *p);
            // 把下标 idx 处的元素拷贝到 p 指向的内存
            // 映射：zarray_get(ds.get(), i, &d)——za 是检测结果数组，idx 是 i，p 是 &d（apriltag_detection_t* d 这个指针变量的地址）
            // za数组里存的是指针（apriltag_detection_t*），所以这行是把第 i 个检测的指针值拷进 d，之后 d->id、d->p 就能用了
            // p 是 void*，函数本身不知道类型，只按元素大小拷字节
            zarray_get(ds.get(), i, &d);
            // 白名单：allowed_ids_ 里没有这个 d->id 就 continue 跳过（id过滤）
            // std::set::count：size_type count(const Key& key) const; 集合里有这个 key 返回 1，没有返回 0
            // 映射：allowed_ids_ 是 std::set<int>，allowed_ids_.count(d->id)——id 在白名单里返回 1，!1是 false 就不跳过；id 不在返回 0，!0 是 true 就 continue
            if (!impl_->config_.allowed_ids_.count(d->id))
                continue;
            // 建一个 TagObservation t：stamp_ 填进来，id_ 是检测到的 id
            TagObservation t;
            t.stamp_ = stamp;
            t.id_ = d->id;
            // side_m_：按 id 去 side_m_ 字典里查这个 TAG 的物理边长，查到了就填，查不到就留默认值（后面 estimate_exit 会以 METRIC_SIZE_MISSING_OR_INVALID 拒绝它）
            // std::map::find + end()（边长查找）
            // iterator find(const Key& key);  iterator end();
            // find 找到返回指向那个元素的迭代器，找不到返回 end()（尾后迭代器）
            // 映射：side_m_ 是 std::map<int, double>（id → 边长米），side_m_.find(d->id) 查这个 id 的边长；side != side_m_.end() 说明查到了，side->second 就是边长（double，米）
            // 查不到就不填，t.side_m_ 留默认值 0
            // std::map 里每个元素都是一个 pair<const Key, T>，.first 是 key，.second 是 value
            auto side = impl_->config_.side_m_.find(d->id);
            if (side != impl_->config_.side_m_.end())
                t.side_m_ = side->second;
            // 四个角点从 d->p（AprilTag 的角点数组）拷进 t.corners_
            // corners_ 是 std::array<cv::Point2d, 4>，corners_[p] 是 std::array::operator[]，返回第 p 个 Point2d 的引用（不做越界检查，跟 C 数组一样）
            // d->p 是 double p[4][2]（AprilTag 头文件写明：四个角点，像素坐标，逆时针绕 TAG 一圈）。p[p][0] 是 x，p[p][1] 是 y
            // det->p[0] ↔ 理想角 (-1, 1)；det->p[1] ↔ 理想角 (1, 1)； det->p[2] ↔ 理想角 (1, -1)；det->p[3] ↔ 理想角 (-1, -1) y是朝下的（FLIP Y）
            // {x, y} 是 cv::Point2d 的列表初始化，调的是 Point2d(double x, double y)
            for (size_t p = 0; p < 4; ++p)
                t.corners_[p] = {d->p[p][0], d->p[p][1]};
            // valid_quad 校验：角点不成合法四边形（不有限/出界/不凸）就跳过（跟 MARK 用的是同一个 valid_quad）
            if (!detail::valid_quad(t.corners_, bgr.size())) // bgr.size(): Size size() const; 返回 Size(cols, rows)，比如 1440×1080 的图返回 Size(1440, 1080)(把图像尺寸传给 valid_quad 用来判断角点是否越界)
                continue;
            // 计数 ++counts[t.id_]
            ++counts[t.id_];
            out.push_back(t); // 把填好的 TagObservation t 拷进 out
        }

        // 同 id 去重（counts；只保留恰好出现一次的 id）
        // 同一帧里同一个 id 出现两次，说明检测器把同一个 TAG 认了两遍（或者有两个 TAG 顶着同一个 id），分不清哪个是真的，干脆两个都不要
        // std::remove_if: template<class ForwardIt, class UnaryPredicate> ForwardIt remove_if(ForwardIt first, ForwardIt last, UnaryPredicate p);
        // 作用：把 out 里所有满足 p 的元素移到末尾，返回新尾迭代器，out.erase() 再把末尾的元素删掉
        // 把 [first, last) 里让 p 返回 true 的元素“挪走”
        // 注意它不改容器大小，只是把要保留的元素往前搬，返回一个迭代器指向“新末尾”（最后一个保留元素的后一个位置）
        // [first, last) 里 p 返回 false 的元素按原顺序搬到前面，尾巴上剩下的元素处于“被移走后”的未指定状态，得靠 erase 真正删掉
        // std::vector::erase: iterator erase(const_iterator first, const_iterator last); 删掉 [first, last) 这段元素，容器大小随之缩小
        // 映射：out.erase(新末尾, out.end())——删掉 remove_if 返回的新末尾到 out.end() 之间那段（也就是被移走的、要丢掉的元素）
        // erase-remove 惯用法：remove_if 负责挑，erase 负责删
        // iterator: begin() 指向第一个元素，end() 指向尾后（最后一个元素的后一个位置，不指向任何元素）,[begin(), end()) 就是整个 vector 的范围
        // lambda:[&]：按引用捕获——lambda 体里用到的外部变量（这里是 counts 这个 map<int, int>）都按引用抓进来，不拷贝(引用捕获就是直接用外面那个 map);(const auto &t)：参数(调用方（remove_if）给的).auto 让编译器推导类型，这里是 const TagObservation&（out 的元素类型）,const & 是只读引用，不拷贝;{ return counts[t.id_] != 1; }：返回 bool。counts[t.id_] 是这个 id 的出现次数（operator[]),!= 1 是 true 说明这个 id 出现过不止一次（因为 t 在 out 里，次数至少是 1，所以 != 1 就是 ≥2，即重复）——返回 true，remove_if 就把它挪走
        // 两种输入：t（要判定的元素，remove_if 每次递一个）和 counts（判定的依据，从外面捕获的）。counts[t.id_] != 1 就是拿 remove_if 递进来的 t 的 id，去捕获进来的 counts 表里查次数。
        out.erase(std::remove_if(out.begin(), out.end(),
                                 [&](const auto &t)
                                 {
                                     return counts[t.id_] != 1;
                                 }),
                  out.end());
        // 把去重后的 vector 返回
        return out;
    }

    // 把一个检到的 TAG（TagObservation）变成位姿观测（PoseObservation）的函数（TAG 版的 estimate_mark）
    PoseObservation estimate_exit(const TagObservation &t, cv::Size size,
                                  const PoseEstimator &estimator)
    {
        // 边长无效，先返回
        // 如果这个 TAG 的物理边长没配（detect() 里 side_m_ 没查到，留默认值）或者不合法（≤0、非有限），就没法算 metric 位姿（PnP 要知道真实尺寸才能算出米制的平移）
        if (!std::isfinite(t.side_m_) || t.side_m_ <= 0) // std::isfinite： template<class T> bool isfinite(T x); 判断一个浮点数是不是有限的（不是 inf，也不是 NaN）
        {
            // 手写一个观测返回
            PoseObservation p;
            p.stamp_ = t.stamp_; // 这个 TAG 的 stamp
            p.source_ = "APRILTAG";
            p.reason_ = "METRIC_SIZE_MISSING_OR_INVALID";
            // estimate_ 留空 → 这个观测是 UNAVAILABLE
            return p;
        }

        // 边长有效，走 PnP
        double h = t.side_m_ / 2;  // 边长的一半
        // model 是 TAG 在自身坐标系里的四个角点：一个边长 2h、中心在原点的正方形，z 都是 0；对应：逆时针对逆时针
        // （跟 MARK 的左上角原点不同，TAG 的模型是中心原点，跟 AprilTag 的约定一致）
        std::array<cv::Point3d, 4> model{{{-h, h, 0}, {h, h, 0}, {h, -h, 0}, {-h, -h, 0}}};
        // 调 estimator.estimate_planar(...)——就是 MARK 用的同一个 estimate_planar，入参是模型点、检到的角点、图像尺寸、stamp、目标 id
        auto out = estimator.estimate_planar(model, t.corners_, size, t.stamp_);
        // 把返回的 source_ 改写成 "APRILTAG"（estimate_planar 里先填的是 "PLANAR"），然后返回
        out.source_ = "APRILTAG";
        return out;
    }

}
