// 当前主干
// 视频逐帧流经检测、位姿和跟踪；输出独立记录每帧结果（observations、tracks、summary），叠加仅用于观察（overlay）。
#include "offline/offline_runner.hpp"
#include "vision/pose/pose_io.hpp"
#include "vision/config/detector_config.hpp"
#include "vision/detection/detector.hpp"
#include "rendering/debug_renderer.hpp"
#include "reporting/run_console.hpp"
#include "export/video_exporter.hpp"
#include "vision/pose/pose_estimator.hpp"
#include "vision/pose/tag_detector.hpp"
#include "vision/tracking/target_tracker.hpp"
#include "io/tracking_io.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <opencv2/videoio.hpp>

namespace mark
{
    int run_offline(const AppConfig &app, const std::string &video,
                    const std::filesystem::path &config_dir)
    {
        // 函数局部类型别名；steady_clock 是<chrono> 库里的一个单调时钟（system_clock 是挂钟，系统时间被 NTP 或人调了会跳变，可表示“现在几点”），用于量耗时
        // 给整个离线运行装了个停表：开头掐表、结尾读数，记到汇总里
        using Clock = std::chrono::steady_clock;
        // cv::VideoCapture 是 OpenCV 的视频读取类：构造时打开视频文件（路径），读头信息（编码、分辨率、帧率），不一次性把整个视频读进内存，之后靠 capture.read(image_) 一帧一帧解码（读到末尾或出错时返回 false，循环结束）
        cv::VideoCapture capture(video);
        if (!capture.isOpened())
            throw std::runtime_error("INPUT_OPEN_FAILED: " + video);
        const double fps = capture.get(cv::CAP_PROP_FPS); // 取视频的帧率（get查询属性），标称值（来自文件元数据），不是实测的，可能坏；后面可用在打开 overlay 视频导出器上，让输出视频跟输入同速
        if (!std::isfinite(fps) || fps <= 0)
            throw std::runtime_error("INPUT_FPS_INVALID");
        // 初始化检测器、位姿求解器、标记检测器（可选）、跟踪器；配置文件在 config_dir 下
        detector::Detector detector(app.detector_config_);  // 在帧循环前构造一次，配置整个运行期间不变；之后每帧调 detector.process(...) 做检测

        const auto pose_config = pose::load_pose_config(config_dir / "pose.yaml");
        pose::PoseEstimator solver(pose_config);// 在帧循环前构造一次，配置整个运行期间不变；之后每帧调 solver.estimate_mark(...) 做位姿求解

        std::unique_ptr<pose::TagDetector> tag_detector;  // std::unique_ptr<T>——<memory> 里的智能指针模板，独占一个堆上分配的 T 类型对象，默认初始化成空指针（可选）
        if (!pose_config.tag_.allowed_ids_.empty())
            tag_detector = std::make_unique<pose::TagDetector>(pose_config.tag_);  // 条件创建：std::make_unique 在堆上构造一个 TagDetector（拿 tag 配置），返回的 unique_ptr 赋给 tag_detector，接管所有权（移动赋值）
        
        const auto tracking_config = tracking::load_tracking_config(config_dir / "tracking.yaml");
        if (!tracking_config.profile_)
            std::cout << "Tracking profile未配置：跳过跟踪数值计算\n";  // std::optional<T> 定义了 explicit operator bool() const：有值返 true，空返 false
        tracking::TargetTracker tracker(tracking_config);  // 在帧循环前构造一次(卡尔曼跟踪器)，配置整个运行期间不变(tracker 有状态)；之后每帧调 tracker.step(...) 做跟踪
        // 建输出目录（包成 path、建好目录，后面用于拼路径）
        const std::filesystem::path directory(app.offline_.run_directory_); // 把配置里的 run_directory_ 字符串（offline.yaml 的 "outputs/marker"）包成一个 std::filesystem::path 对象（path 是 C++17 的路径类型，带路径操作）
        std::filesystem::create_directories(directory); // 创建这个目录（create_directories会把整条链都建出来，相当于 Linux 的 mkdir -p；单数的只建最后一层，父目录不存在会失败；建失败会抛异常）
        // 开输出（四个）
        // observations 和 tracks_file 是“先开文件、写好头、列表开着等循环写”；exporter 是“先创建，等循环里第一帧再 open”；summary 是“循环里累计，结束后一次性写”
        VideoExporter exporter; // 叠加视频的输出器→ overlay.avi（叠加视频）
        RunSummary summary; // 运行汇总：帧数、检测、位姿、轨迹、原因的计数→ summary.yaml（运行汇总）
            // 开 observations.yaml 开始写，并写文件头（schema_version 6、stream_type "PoseObservations"、position_unit "m"、time_unit "us"、frames "["）→ observations.yaml（每帧的位姿观测）
        cv::FileStorage observations((directory / "observations.yaml").string(),
                                     cv::FileStorage::WRITE); // FileStorage 要的是字符串文件名；写模式，创建/覆盖这个文件；在整个运行期间一直开着（循环里每帧往里追加）
        observations << "schema_version" << 6 << "stream_type" << "PoseObservations"
                     << "position_unit" << "m" << "time_unit" << "us" << "frames" << "["; // 写键 "frames"，再写 "[" 开始一个列表（循环追加）
            // 开 tracks.yaml 开始写，并写文件头（schema_version 2、stream_type "TrackingResult"、frames "["）→ tracks.yaml（每帧的跟踪结果）
        cv::FileStorage tracks_file((directory / "tracks.yaml").string(), cv::FileStorage::WRITE); // 跟踪结果里不额外标单位
        tracks_file << "schema_version" << 2 << "stream_type" << "TrackingResult" << "frames"
                    << "[";
        // 记录开始时间，后面算耗时
        const auto start = Clock::now(); // Clock::now() 是个静态函数，返回“现在这个时间点”，类型为 std::chrono::steady_clock::time_point（时间点）
        // 帧图像的缓冲，每轮循环由 capture.read 填(覆盖)
        cv::Mat image_; // 默认构造（0 行 0 列，没有像素数据）
        // 正式进入帧循环部分（处理每一帧）！！！
        for (uint64_t id = 0; capture.read(image_); ++id)  // read() 读一帧 + 判断要不要继续
        {
            // 精确推断时间戳，单位是微秒（us）（用帧号和 FPS 算出来的名义时间；后续换成相机驱动给的真实时间（曝光时刻、接收时刻），填进 CaptureStamp 的 exposure_us_ 和 time_uncertainty_s_）
            // 用 long double（扩展精度浮点，比 double 精度高）是为了让这一步的分数部分算得准，下一行再用 std::llround 四舍五入成整数微秒（llround 是“round to long long”，取最接近的整数）
            const long double timestamp = static_cast<long double>(id) * 1e6L / fps; 
            // 聚合初始化FrameInput（cv::Mat（头部（行数、列数、类型、一个指向像素数据的指针）+像素数据（存在堆上，带引用计数））复制浅拷贝，只复制头部（几十个字节）并把引用计数 +1，两个 Mat 的数据指针指向同一块像素（共享））
            detector::FrameInput input{image_, id, std::llround(timestamp)};  // 微秒时间戳（四舍五入取整后,int64_t）放进 FrameInput，之后它会变成 CaptureStamp 里的 received_us_（接收时间）
            
            // 整个检测板块：校验输入、预处理、找 MARK、拟合角点
            const auto raw = detector.process(input);
            summary.add(raw); // 汇总检测结果：总帧数、检测个数、状态计数、诊断原因计数

            // 准备位姿输入（准备这帧的两个空容器，再填一张“帧身份证”CaptureStamp——后面这帧所有位姿观测都带着它（出处））
            // 两个空向量：每帧都在循环里重新声明，帧帧清空，不跨帧带状态
            std::vector<pose::PoseObservation> poses_; // poses_ 装这帧的位姿观测（MARK 的 estimate_mark + TAG 的 estimate_exit 往里塞）
            std::vector<tracking::TargetTrack> tracks_; // tracks_ 装这帧的跟踪结果（后面 tracker.step 的返回值赋给它）
            // 填 CaptureStamp 字段：三个字符串（session、相机、时钟域）+ 像素域 + 帧号 + 接收时间 + 两个 optional（曝光、时间误差）
            pose::CaptureStamp stamp;
            // 时间戳的时钟域——这时间是帧号/FPS 推算出来的名义时间，不是真实相机时钟，所以标成 "video-fps" 这个域
            // 像素域——图像是原始带畸变的（没做去畸变），告诉 solvePnP 要用畸变系数；PixelDomain 只有两个值：Unknown（默认）和 RawDistorted
            stamp.frame_id_ = id; // 帧号
            stamp.received_us_ = input.timestamp_us_; // 算出来的名义时间戳（微秒）
            // 帧号/FPS 生成录像名义时间；没有曝光或时间误差的实测信息。

            // 整个位姿板块
            // 位姿估计：把检测结果变成位姿观测，塞进 poses_
            // MARK 位姿（循环）：对检测器给出的每个检测，调 solver.estimate_mark 算一次位姿（里面是 IPPE PnP + S）
            for (size_t i = 0; i < std::max(size_t(1), raw.detections_.size()); ++i) // 注意循环下限是 max(1, …)——哪怕这帧一个检测都没有，也至少跑一次，estimate_mark 会返回一个带原因（比如没检到）的空观测
                poses_.push_back(solver.estimate_mark(raw, i, image_.size(), stamp)); // 这样每帧 poses_里至少有一条，原因能进 summary，而不是空列表。
            // 出口 TAG（if 分支）:调用tag_detector->detect() 检测出口 TAG，调 pose::estimate_exit 算位姿，塞进 poses_；如果没有 tag_detector（配置里没出口 TAG），就跳过
            // 如果有 TAG 检测器，检出出口 TAG 后调 estimate_exit 算位姿，同样塞进 poses_
            if (tag_detector) // TAG 检测器可选的，是个可能为空的指针
                for (const auto &tag : tag_detector->detect(image_, stamp)) // 在这帧图像里找出口 TAG，返回一个 TagObservation 的列表——找到几个就返回几个，没找到就返回空列表（没有 MARK 循环那个 max(1, ...) 保底，0 个 TAG 就是 0 个观测）
                    // 给找到的每个 TAG 算位姿；入参是 TAG 观测（id、四个角点）、图像尺寸、还有 solver（PoseEstimator）；内部调的也是 estimate_planar（跟 MARK 同一套 PnP 机器，只是角点来自 TAG），返回的也是 PoseObservation，跟 MARK 的观测是同一种东西
                    poses_.push_back(pose::estimate_exit(tag, image_.size(), solver)); // 把 TAG 的位姿观测追加进 poses_，跟 MARK 的观测放同一个 vector 里

            // 汇总位姿结果：每帧的位姿观测状态计数（ESTIMATED / UNAVAILABLE）
            // 位姿计数：把这帧的每个位姿观测按状态计数，累进 summary.poses_ 这个 map 里
            // poses_ 是这帧的观测（MARK 的 estimate_mark + TAG 的 estimate_exit 塞进来的），pose_status_name(p) 把每个观测的状态（ESTIMATED / UNAVAILABLE）转成字符串当 key，++ 就是计数
            // 跑完整个视频，summary 里就有“多少帧 ESTIMATED、多少帧 UNAVAILABLE”的总数，最后写报告用
            // 注意 summary.poses_[key]：map 的 [] 遇到不存在的 key 会先建一个计数 0 的项，再 ++ 变成 1，所以不用先判存在
            for (const auto &p : poses_)
                ++summary.poses_[pose::pose_status_name(p)]; // MARK 的缺席要报，TAG 的缺席不用报——缺席是 TAG 的常态，出现才是事件
            
            // 跟踪
            tracking::TrackingFrame frame; // TrackingFrame 打包盒（纯数据结构），只装两样：stamp_（这帧的 CaptureStamp）+ observations_（这帧的位姿观测，也就是 poses_，含 UNAVAILABLE 的也照样传进去）
            frame.stamp_ = stamp;         // 装进这帧的 CaptureStamp（时间戳）
            frame.observations_ = poses_;  // （拷贝）装进这帧的全部位姿观测（poses_，MARK + TAG，含 UNAVAILABLE 的也照样传进去——筛选是跟踪器内部的事，offline_runner 不替它筛）
            // 核心（每帧必调一次）
            // tracker 是 setup 里建好的 TargetTracker
            // 有量测就预测+更新，没量测就只预测（coast）——状态往前推、协方差 P 变大，但不更新
            // 检测断档几帧，跟踪不会停，只会越来越不确定
            const auto result = tracker.step(frame); // 无检测也推进coast与协方差。注意：result 里除了 tracks_，还有个 events_（诊断事件，比如 NIS 拒绝），后续这两样汇总进 summary：按跟踪状态（MEASURED / COASTING / LOST）计数进 summary.tracks_，按事件字符串计数进 summary.reasons_
            // important:滤波之后的位置（平滑过的）+ 速度（推算出来的）+ 协方差 P（可信度）
            tracks_ = result.tracks_; // 这帧的跟踪结果（TargetTrack 列表，每条有 track_id、状态、位置、协方差、可选的速度），存进 tracks_ 供后面画 overlay 用

            // 跟踪计数 + 写 tracks.yaml
            // 把这帧的跟踪状态（MEASURED / COASTING）数进 summary.tracks_、诊断事件数进 summary.reasons_，然后把这帧的 TrackingResult 写进 tracks.yaml
            // 汇总跟踪结果：每帧的跟踪状态计数（MEASURED / COASTING）和事件计数（诊断原因）
            // 跟踪计数：
            // 按跟踪状态（MEASURED / COASTING / LOST（已经删掉））计数进 summary.tracks_，按事件（result.events_ 里的诊断字符串，比如 NIS 拒绝之类）计数进 summary.reasons_
            for (const auto &t : tracks_) // 状态计数
                // summary.tracks_ 是 map<string, 计数>，operator[] 按状态名取计数（没有就默认建个 0），++ 加一
                // track_state_name和 pose_status_name 一样，返回 const char*（"MEASURED" / "COASTING"），因为返回的是字符串字面量
                ++summary.tracks_[tracking::track_state_name(t.state_)];
            for (const auto &event : result.events_) // 事件计数
                // result.events_ 里的诊断字符串（比如 TRACK_CREATED、TRACK_MATH_REJECTED_NO_COMMIT、DUPLICATE_OR_REORDERED_FRAME 这些）各出现几次，数进 summary.reasons_；最后汇总打印的时候就能看到"这一跑里每种事件发生了多少次"
                ++summary.reasons_[event];
            // write_tracking_result 把这帧的跟踪结果写进 tracks.yaml；把这帧的 TrackingResult（tracks_、events_、association_nis_ 这些）写进 tracks.yaml
            tracking::write_tracking_result(tracks_file, result); // 写 YAML

            // YAML 写观测 + overlay + 循环收尾

            // 写观测 YAML：每帧把 poses_（MARK + TAG 的位姿观测）写进 observations.yaml
            // 每帧把 poses_ 里的全部位姿观测（MARK + TAG，含 UNAVAILABLE 的）写进 observations.yaml——外层一个 map，里面是这帧的 stamp + 一个 observations 列表，每个观测是 write_pose_observation 写的一个小 map
            observations << "{" << "stamp";
            pose::write_capture_stamp(observations, stamp); // 开 map + 写 stamp：observations << "{" 打开一个 map，"stamp" 是 key，write_capture_stamp把这帧的 CaptureStamp 写进去（
            observations << "observations" << "[";
            for (const auto &p : poses_)
                pose::write_pose_observation(observations, p); // observations 列表： "observations" 是 key，fs << "[" 打开一个序列，循环里每条观测调一次 write_pose_observation 写进去；这里不过滤（poses_ 里有什么就写什么，MARK 和 TAG、ESTIMATED 和 UNAVAILABLE 全写，筛选内部处理）
            observations << "]" << "}"; // 关 map："]" 关 observations 序列，"}" 关外层 map，一帧写完

            // overlay 写视频帧
            // 开了 overlay 才跑：第 0 帧打开视频导出器，之后每帧把渲染好的调试帧（原图 + 检测框 + 跟踪结果）写进去
            if (app.offline_.overlay_) // 配置开关，关了不出视频
            {
                // 开了才跑：
                // 第 0 帧（id == 0）先 exporter.open(...) 打开导出器——输出文件是 directory / video_filename_（就是 overlay.avi），尺寸是 image_.size()，帧率是 fps
                if (id == 0)
                    exporter.open(directory / app.offline_.video_filename_, image_.size(), fps);
                // 之后每帧：render_debug_frame(image_, raw, poses_, tracks_, &pose_config) 把这帧渲染成调试图（原图 + 检测框 + 跟踪结果），exporter.write(...) 写进视频
                // render_debug_frame 是项目内定义的渲染函数（画框、画轨迹、画文字那个），exporter 是 setup 块里建好的 VideoExporter（先写 .partial.avi 临时文件、最后原子改名）
                exporter.write(render_debug_frame(image_, raw, poses_, tracks_, &pose_config));
            }
        } // 循环结束：帧循环 for (id; capture.read(image_); ++id) 的收尾——视频读完（capture.read 返回 false），循环结束，跳到后面的收尾块

        // 收尾
        // 循环之后：收掉视频导出器、关掉两个 YAML 文件的序列、算总耗时、写 summary.yaml、控制台打印汇总、return 0
        // 收视频导出器:开了 overlay 并且确实跑了帧（summary.frames_ > 0），才调 exporter.finish()——把 .partial.avi 临时文件收尾、原子改名成最终的 overlay.avi
        if (app.offline_.overlay_ && summary.frames_) // 没开 overlay 或者一帧都没跑，就不用收
            exporter.finish();
        // 关 YAML 序列:关掉 observations.yaml 和 tracks.yaml 外层的序列（[ 是在 setup 块里打开的，每帧往里写一个 map，这里循环结束了一次性关上）
        observations << "]";
        tracks_file << "]";
        // 算总耗时:Clock::now() - start 是这一跑的总耗时，duration<double> 转成 double 秒，.count() 取数值，存进 summary.elapsed_s_
        // std::chrono::steady_clock：using Clock = std::chrono::steady_clock; Clock::now(); 返回一个 time_point（时间点）
        // steady_clock 是单调时钟——只往前走，永远不回拨、不跳变（和 system_clock不一样，系统时钟会被 NTP 对时调来调去）,用来耗时
        // 这里Clock::now() 取当前时间点，Clock::now() - start 两个时间点相减得到一个 duration（时间段）；start 是 const auto start = Clock::now();，循环开始前记的时间点
        // std::chrono::duration<double> + .count()：template<class Rep, class Period = std::ratio<1>> class duration;
        // duration 是个模板：Rep 是数值类型，Period 是单位（默认 ratio<1> = 秒）
        // std::chrono::duration<double>(Clock::now() - start)：把 steady_clock 的时间段（底层单位一般是纳秒）转成以秒为单位的 double。比如跑了 2.5 秒，这里就是 2.5
        // .count()：Rep count() const;——取出这个时间段的数值（这里是 double 秒数）
        summary.elapsed_s_ = std::chrono::duration<double>(Clock::now() - start).count();
        // 写汇总 + 打印 + 返回
        // write_run_summary：把 summary（帧数、各状态计数、各事件计数、总耗时这些）写进 summary.yaml
        // std::filesystem::path 的 / 和 .string()：path operator/(const path& lhs, const path& rhs);  string string() const;
        // directory / "summary.yaml"：/ 把两个路径拼起来，中间自动加目录分隔符，得到 directory/summary.yaml
        // .string()：把 path 转成 std::string（write_run_summary 要的是 string 路径，不是 path 对象）
        write_run_summary(summary, (directory / "summary.yaml").string());
        // write_run_console：同样的汇总打印到控制台（std::cout），跑完能在终端直接看到；std::cout：标准输出流（终端），write_run_console(std::cout, summary)就是把汇总打印到终端上。
        write_run_console(std::cout, summary);
        // 程序正常结束
        return 0;
    }

} // namespace mark
