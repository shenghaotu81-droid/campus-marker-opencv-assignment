// 按时间顺序推进轨迹；有位置量测则更新，无量测则预测，并保留数值失败前的状态。
#include "vision/tracking/target_tracker.hpp"
#include "filter/kalman_cv.hpp"
#include "measurement/measurement_adapter.hpp"
#include <stdexcept>

namespace mark::tracking
{
    // 自由函数（const char*、字符串字面量、noexcept）
    // 把跟踪状态（两种）翻成字符串，给 offline_runner 计数用
    const char *track_state_name(TrackState s) noexcept
    {
        return s == TrackState::Measured ? "MEASURED" : "COASTING";
    }

    // 匿名 namespace 里三个小 helper
    // 只给这个文件内部用（State::advance 调它们）
    namespace
    {
        // elapsed(a, b)：算两个时间戳（微秒，int64_t）的差，返回秒（double）
        // advance 里用来算 dt（两帧之间的时间间隔）
        double elapsed(int64_t a, int64_t b)
        {
            return double((static_cast<long double>(a) - b) / 1e6L); // 先转 long double 再减（防溢出、保精度），除以 1e6L（微秒→秒，L 是 long double 字面量），再转回 double
        }
    }

    // 跟踪器的内部状态 + 干活的方法：
    // filter_：卡尔曼滤波器（detail::CvFilter）；live_ / confirmed_：轨迹是否存活、是否已确认；id_ / next_id_：当前 track id 和下一个 id 的计数器；
    // state_time_、last_input_、last_measurement_、seed_：时间轴和最近的输入/量测；
    // advance(f, config)：每帧真正推进的方法（预测/更新、状态机、NIS 门都在里面）
    struct State
    {
        // State 的成员
        // filter_（detail::CvFilter）——卡尔曼滤波器，跟踪器的核心
        // 存着轨迹的状态估计（位置+速度，6 维）和协方差 P
        // predict 推进它，correct/bootstrap/initialize 更新它
        detail::CvFilter filter_;
        // live_（默认 false）——有没有存活的轨迹；false = 没轨迹（还没见过量测，或被 reset 了）；true = 正在跟一条轨迹
        // confirmed_（默认 false）——轨迹确不确认；第一帧量测建轨时 live_ = true、confirmed_ = false；第二帧量测 bootstrap 算出速度后才置 true；只有确认了的轨迹才对外报速度
        bool live_ = false, confirmed_ = false;
        // id_（默认 0）——当前轨迹的编号，建轨时 id_ = next_id_++ 分配
        // next_id_（默认 1）——下一个要分的编号，每建一条新轨迹加 1；从 1 开始，所以第一条轨迹是 1 号
        uint64_t id_ = 0, next_id_ = 1;
        // state_time_（默认 0）——滤波器当前状态的时间（微秒，采样时间）；predict 算 dt 靠它（dt = now - state_time_），每帧更新为 now
        int64_t state_time_ = 0;  // 滤波器自己的时钟
        // last_input_（optional<CaptureStamp>）——上一帧输入的 stamp
        // 给 same_context 检查和重复/乱序检查用
        // 还没处理过任何帧时是空的；reset和 CONTEXT_RESET 会把它清空
        std::optional<pose::CaptureStamp> last_input_; // 上一个被接受的输入帧的完整 stamp；每帧被接受就更新，不管有没有量测
        // last_measurement_（CaptureStamp）——上一次有量测那帧的 stamp（也就是上次真正更新滤波器的那帧，不是只预测）
        // bootstrap 算 dt 用它（种子量测到当前量测的时间差），输出的 TargetTrack 里也记它
        pose::CaptureStamp last_measurement_; // 上一次有量测（滤波器被更新/纠正，不只是预测）那帧的 stamp（只有有量测的帧才更新，coast 的帧不更新）
        // seed_（PositionMeasurement）——种子量测（建轨时的第一个量测，或最近一次的量测）
        // bootstrap 用它和第二个量测算初始速度（seed_ 是第一个，m 是第二个），每次更新后更新为最新量测
        detail::PositionMeasurement seed_;

        // 核心方法：每帧推进轨迹
        TrackingResult advance(const TrackingFrame &f, const TrackingConfig &c)
        {
            TrackingResult out;
            out.input_stamp_ = f.stamp_; // 先记录结果的帧身份（无论接受/拒绝）

            // 时间戳校验（INVALID_FRAME_STAMP）
            // 验帧的时间字段：received 为负、曝光时间戳晚于 received、时间不确定度非有限或为负，任一成立就拒绝这一帧，记事件直接 return（不接受输入、不建轨迹）
            // 防坏时间数据污染滤波时间轴
            // 时间字段来自帧输入；排除负时刻、曝光倒置和非法误差界，防止污染滤波时间轴。
            const auto &stamp = f.stamp_;
            if (stamp.received_us_ < 0)     // 收到时间是负数，时间戳本身就是坏的（微秒数不可能为负）
            {
                out.events_.push_back("INVALID_FRAME_STAMP");
                return out; // 跟踪器的状态原样保留，只拒绝这帧（return）
            }

            // 重复/乱序帧检查（DUPLICATE_OR_REORDERED_FRAME）
            // last_input_存在且帧号没递增、采样时间没递增、或 received 时间倒退，拒绝这一帧，记事件直接 return
            // 波按时间顺序推进，乱序帧会搞坏时间轴。
            if (last_input_ &&  // last_input_ 存在才检查（第一帧没有上一帧，直接放行）
                (f.stamp_.frame_id_ <= last_input_->frame_id_ || // 帧号没递增；等于就是重复帧（同一帧来了两次），小于就是乱序（后面的帧先到了）；帧号应该是严格递增的
                 detail::sample_time(f.stamp_) <= detail::sample_time(*last_input_) ||  // 采样时间没递增；sample_time 是量测的有效时刻（有曝光时间戳就用曝光的，没有就用 received；滤波器用的时间——predict 的 dt、bootstrap 的 dt 用它算的）；等于说明量测时刻没前进（dt = 0，没新信息），小于说明时间倒流了
                 f.stamp_.received_us_ < last_input_->received_us_))  // 收到时间倒退了；帧到达的顺序乱了
            {
                out.events_.push_back("DUPLICATE_OR_REORDERED_FRAME");
                return out; // 这帧等于没来过，状态原样保留
            }

            // 接受输入
            // 先存 previous（给预测算时间界用），last_input_ 更新为这帧，input_accepted_ = true（step 据此决定提交）
            last_input_ = f.stamp_; // 更新
            out.input_accepted_ = true; // 标记接受（step 决定提交的开关：true 才把 candidate 移回 impl_->state_）
            // 如果没有 profile（!c.profile_），没东西可跟踪，直接 return
            // 注意这时候 input_accepted_ 已经是 true 了，所以 step 会提交（last_input_ 的更新生效），但 tracks_ 是空的——这一帧被接受了、记下来了，只是不产生轨迹
            if (!c.profile_)
                return out;

            // 预测
            // 有存活轨迹才做：filter_.predict(dt, ...) 把滤波器状态推到这帧的时间
            // 只要 live_ 是 true，每帧都 predict——有量测是"predict + correct"，没量测是"只 predict、不 correct"，也就是 coast 往前滑
            // dt 是距上次状态时间的间隔，时间不确定度（bound）也传进去；state_time_ 更新为现在
            const auto &p = *c.profile_; // 取跟踪配置（R/Q 参数那些）
            const auto now = detail::sample_time(f.stamp_); // 这帧的采样时间（量测的有效时刻）
            // if (live_) 才预测——没有存活的轨迹就没东西可推（滤波器还没初始化，第一帧量测在后面才会初始化它）
            if (live_)
            {
                double dt = elapsed(now, state_time_); // 距滤波器上次状态时间的间隔，就是这次要往前推多远
                // 把滤波器状态往前推 dt，过程噪声用 acceleration_psd_m2ps3_（q）
                filter_.predict(dt, p.acceleration_psd_m2ps3_);
            }
            state_time_ = now; // 滤波器的状态时间更新为现在（不管有没有轨迹都执行——这样即使这帧没轨迹，state_time_ 也跟着帧走，下次建轨之后 dt 才算得对）

            // 适配量测 + 时间不确定度放大 R
            // adapt_measurements 把这帧的位姿观测转成滤波器能用的位置量测；
            // 再对每个量测，如果有时间不确定度（b > 0），用 time_augmented_noise 把 R 放大（时间戳不准 → 目标在这段时间里动了 → 位置更不确定）
            auto measurements = detail::adapt_measurements(f, c); // 把这帧的位姿观测转成滤波器能用的位置量测（按 source/target 筛选，取位置 t_m_ 和 R）

            // 更新
            // 没存活轨迹：恰好 1 个量测就新建轨迹（分 id、seed 滤波器、记 TRACK_CREATED，未确认）
            // 有存活轨迹且有量测：按 NIS 分数挑最好的一个（平局就不更新）；未确认就 bootstrap（用两次量测算出速度），已确认就 correct（卡尔曼更新）
            // update 块分两条路：没存活轨迹时，恰好 1 个量测就新建轨迹（只 initialize，不算速度）；有存活轨迹且有量测时，按 NIS 分数挑最好的一个量测，未确认就 bootstrap（用两次量测算速度），已确认就 correct（卡尔曼更新）。平局（两个量测 NIS 完全相等）时整帧跳过更新。
            bool measured = false;
            // 新建轨迹
            if (!live_)
            {
                // 只有恰好 1 个量测才建轨（0 个没东西建，多个不知道选哪个，干脆不建）
                if (measurements.size() == 1)
                {
                    // 分个 id，seed_ 记下这个量测（留着下一帧 bootstrap 用），filter_.initialize 用它的位置和 R 初始化滤波器（速度先验 0，均值方差 vv）
                    id_ = next_id_++;
                    // std::vector::front：reference front(); const_reference front() const;
                    // 返回第一个元素的引用（可读写）。这里 measurements.front() 就是第 0 个量测
                    // 注意：空 vector 上调 front() 是未定义行为——这里能调是因为前面已经 size() == 1 保证了非空
                    seed_ = measurements.front();
                    filter_.initialize(seed_.position_, seed_.R_,
                                       p.initial_velocity_variance_m2ps2_);
                    // live_ 和 measured_置 true，confirmed_ 还是 false（速度还没算出来），记个 TRACK_CREATED 事件
                    last_measurement_ = f.stamp_;
                    live_ = measured = true;
                    confirmed_ = false;
                    out.events_.push_back("TRACK_CREATED");
                }
            } // NIS 选量测
            else if (!measurements.empty())
            {
                // 先拿第 0 个量测算 innovation，再从第 1 个开始逐个比 NIS（score_）：更小的就换成它（best = i，tie = false）；相等的就置 tie = true
                size_t best = 0;
                auto innovation = filter_.innovation(measurements[0].position_, measurements[0].R_);
                bool tie = false;
                for (size_t i = 1; i < measurements.size(); ++i)
                {
                    auto item = filter_.innovation(measurements[i].position_, measurements[i].R_);
                    if (item.score_ < innovation.score_)
                    {
                        innovation = item;
                        best = i;
                        tie = false;
                    }
                    else if (item.score_ == innovation.score_)
                        tie = true;
                } // 通常只有 1 个量测，循环不跑，best 就是 0
                // 更新；平局就整块跳过（这帧不更新）
                if (!tie)
                {
                    // 不平局
                    // 记下这次关联的 NIS（out.association_nis_）
                    auto &m = measurements[best];
                    out.association_nis_ = innovation.score_;
                    // 未确认：bootstrap——用 seed_（上一帧的量测）和当前量测 m，dt 是两帧采样时间的间隔，算出速度，confirmed_ = true
                    if (!confirmed_)
                    {
                        double dt = elapsed(now, detail::sample_time(last_measurement_));
                        filter_.bootstrap(seed_.position_, seed_.R_, m.position_, m.R_, dt,
                                          p.acceleration_psd_m2ps3_);
                        confirmed_ = true;
                    }
                    else // 已确认：correct(innovation)——卡尔曼更新
                        filter_.correct(innovation);
                    // 更新 seed_ 为当前量测（留给下一帧用），last_measurement_ 为这帧 stamp，measured = true
                    seed_ = m; // 备用
                    last_measurement_ = f.stamp_;
                    measured = true;
                }
            }

            // 组装输出
            // build-output：有存活轨迹就把滤波器当前状态打包成一个 TargetTrack（track_id、MEASURED/COASTING、位置、协方差、确认了才有速度），推进 out.tracks_，返回 out；
            // 没存活轨迹，out.tracks_ 就是空的。
            // 有存活轨迹就把滤波器当前状态打包成 TargetTrack（track_id、MEASURED/COASTING、位置、协方差、确认了才有速度），推进 out.tracks_，返回
            if (live_)
            {
                TargetTrack t;
                t.track_id_ = id_; // 这条轨迹的 id(一般1)
                // 这帧有量测更新过就是 Measured，没量测（纯 predict 滑过来的）就是 Coasting
                t.state_ = measured ? TrackState::Measured : TrackState::Coasting;
                // 上次真正用上量测的那帧的 stamp（不是这帧，是 last_measurement_）
                t.last_measurement_stamp_ = last_measurement_;
                // 这帧的时间 now（滤波器状态的时间）
                t.state_time_us_ = now;
                // 直接从滤波器取当前位置和协方差
                t.position_m_ = filter_.position();
                t.covariance_ = filter_.covariance();
                // 只有 confirmed_ 才填——未确认的轨迹速度是零均值先验，不对外发布
                if (confirmed_)
                    t.velocity_mps_ = filter_.velocity();
                out.tracks_.push_back(t);
            }
            return out;
        }
    };

    // TargetTracker::Impl + 构造/析构
    // pimpl（头文件不暴露实现细节）：硬性要求——凡是会销毁/移动这个 unique_ptr 的函数，都得定义在 Impl 完整的地方
    // Impl 里只装 config_（TrackingConfig 跟踪配置）和 state_（上面那个 State）
    struct TargetTracker::Impl
    {
        // 配置和状态分开，一个是只读的输入、一个是可变的工作数据
        TrackingConfig config_; // config_（TrackingConfig）：固定不变的规则——R/Q 参数、阈值等；构造时定下来，之后只读不写
        State state_; // state_（State）：每帧都在变的“记忆”——卡尔曼滤波器、live/confirmed 标志、track id、时间戳；step() 每帧读它、改它
        // 构造函数；explicit 防着 TrackingConfig 被悄悄隐式转成 Impl
        explicit Impl(TrackingConfig c) : config_(std::move(c)) {};
    };

    // make_unique<Impl> 在堆上建出 Impl，把配置 move 进去（by-value + move，调用方把配置交出来，不多拷一份）
    TargetTracker::TargetTracker(TrackingConfig c) : impl_(std::make_unique<Impl>(std::move(c))) // 构造是 make_unique<Impl> 把配置移进去
    {
    }

    TargetTracker::~TargetTracker() = default; // 析构 = default写在 .cpp 里（头文件里 Impl 只是前置声明（不完整））

    // TargetTracker::step（核心入口）调度
    // “先在副本上算，成了才提交”：把 state_ 拷一份叫 candidate，在副本上调 advance；只有 out.input_accepted_ 是 true，才把副本移回 impl_->state_（提交）
    // advance 里要是抛异常（数学计算失败），catch住，返回一个带 "TRACK_MATH_REJECTED_NO_COMMIT:" + e.what() 事件的结果，不提交（跟踪器保持旧状态不变，保留数值失败前的状态）
    TrackingResult TargetTracker::step(const TrackingFrame &f)
    {
        try
        {
            // 状态跨帧保持的逻辑
            // 把当前 state_ 拷一份叫 candidate，在副本上调 advance（真正的每帧推进——预测/更新、状态机、NIS 门）
            auto candidate = impl_->state_; // 拷副本（把当前状态拷一份）
            auto out = candidate.advance(f, impl_->config_); // 在副本上推进（预测/更新、NIS、状态机），不碰原状态
            // 只有 out.input_accepted_ 是 true，才把副本移回 impl_->state_，这一帧的推进才算生效；否则副本直接丢掉（结果输出照常，但是是空的），跟踪器保持旧状态
            // 状态是跟踪器的记忆（被拒绝时保留旧状态），结果是这一帧的输出（被拒绝时是空的）
            if (out.input_accepted_)
                impl_->state_ = std::move(candidate); // 只在接受时提交；抛异常也不污染 commit-or-discard
            return out;
        }
        catch (const std::exception &e)
        {
            // advance 里抛异常（数学计算失败），catch 住，返回一个带 "TRACK_MATH_REJECTED_NO_COMMIT:" + e.what() 事件的结果，不提交——坏掉的一帧不会污染跟踪器的状态
            TrackingResult out;
            out.input_stamp_ = f.stamp_;
            // 先把字面量转成 std::string，再 + e.what()："字面量" + e.what()是两个 const char* 相加，C++ 会当成指针算术（错的）；至少一边得是 std::string（包装），operator+ 才能做字符串拼接
            out.events_.push_back(std::string("TRACK_MATH_REJECTED_NO_COMMIT:") + e.what()); // std::exception::what()：virtual const char* what() const noexcept; std::exception 的虚函数，返回一个描述异常的 C 字符串（const char*）。catch (const std::exception &e) 抓到异常后，e.what() 就是那条错误信息；what() 是虚函数，每个异常类型自己实现——std::runtime_error返回构造时传进去的那串，std::invalid_argument 同理；返回的指针只在异常对象活着的时候有效（这里是 catch 块里），是个 const char* 不是 std::string
            return out;
        }
    }
}
