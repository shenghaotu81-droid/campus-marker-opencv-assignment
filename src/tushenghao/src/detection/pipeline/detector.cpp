// 检测入口校验图像与帧序，维护当前输入上下文，并返回带帧号的四角结果。
// Detector 这个类（入口 + 协调者）主干；“带状态的入口”
/*  大致流程：
校验输入（图是不是空的、是不是 2 维、是不是 CV_8UC3 的彩色图；帧号和时间戳顺不顺）；校验不过就 reset() 并返回一个带原因的 INVALID_INPUT
校验过了就调 run_decode_pipeline(frame, config_)->decode_stage.cpp（核心：管线总调度）->decode_stage.cpp 里调 preprocess/preprocess.cpp（预处理（缩放、二值化））;geometry/match_quad.cpp（多尺度四边形匹配）;corners/ （角点拟合，排序）
最后更新状态（记下这帧的帧号/时间戳和图尺寸）再返回
*/
#include "vision/detection/detector.hpp"
#include "decode_stage.hpp" // 辅助编辑器找路径
#include <optional>

#include "pipeline/decode_stage.hpp"

namespace mark::detector
{
    namespace
    {
        // 当前用来存上一帧的编号和时间（用来验顺序）
        struct FrameStamp
        {
            uint64_t frame_id_{};
            int64_t timestamp_us_{};
        };

        // 校验帧号和时间戳顺序（上一帧的帧号/时间戳在 p 里，当前帧在 c 里）；校验帧号和时间戳顺不顺
        bool validate_frame_stamp(const FrameStamp &c, const std::optional<FrameStamp> &p,
                                  std::string &reason_)
        {
            if (c.timestamp_us_ < 0)
                reason_ = "INVALID_STAMP";
            else if (p && (c.frame_id_ <= p->frame_id_ || c.timestamp_us_ < p->timestamp_us_)) // optional 里有值就解引用，没值就不解引用；原理：先 `p &&`（或 `if (p)`、`p.has_value()`）判有没有值，有值再 `p->成员`（即 `(*p).成员`,-> 其实就是 * + . 的简写）解引用；空的时候解引用是未定义行为，所以必须先判再解。
                reason_ = "INVALID_SEQUENCE";
            else
            {
                reason_.clear(); // reason_ 为空表示校验通过
                return true;
            }
            return false;
        }
    }

    struct Detector::Impl
    {
        // Impl 存Detector的状态
        // impl_->config_（配置,创建时定好，运行中不变）
        DetectorConfig config_;
        // impl_->last_input_（上一帧的帧号/时间戳）
        std::optional<FrameStamp> last_input_;
    };

    // 构造函数：按值拿一份配置，先在堆上建 Impl，再 impl_->config_ = std::move(config_) 把配置移进去（只在 Detector 创建时跑一次）
    Detector::Detector(DetectorConfig config_) : impl_(std::make_unique<Impl>())   // 在堆上新建一个 Impl（成员全是默认值：config_ 默认、last_input_ 空、original_size_ 空），返回一个 unique_ptr 拥有它
    {
        impl_->config_ = std::move(config_);  // 传进来(按值传进来（拷贝进来）)时复制一次（调用者传左值就复制，传右值就移动），进来之后再 move 一次进成员
    }

    // 析构函数（只在 Detector 销毁时跑一次）
    // unique_ptr<Impl> 里的 Impl 在头文件里是不完整类型（只有 struct Impl; 这一句前置声明，编译器知道它存在，但不知道它多大、里面有什么）
    // delete 一个 unique_ptr 时必须知道完整类型，不然编译器不知道怎么释放、怎么调析构
    // 即使析构函数只是 = default，也必须在头文件里声明、在 Impl 完整定义的地方定义
    Detector::~Detector() = default;

    // 每帧跑一次
    FrameResult Detector::process(const FrameInput &frame)
    {
        FrameResult result{};
        // 给校验失败提前返回准备的——校验不过直接 return，result 里就得有正确的帧号和时间戳
        result.frame_id_ = frame.frame_id_;
        result.timestamp_us_ = frame.timestamp_us_;
        result.status_ = Status::INVALID_INPUT;  // 先假设无效

        const FrameStamp stamp{frame.frame_id_, frame.timestamp_us_}; // 当前帧的帧号和时间戳（身份牌：拿去跟上一帧的戳比顺序）；这一帧处理完，这个戳就会被存进 last_input_，成为下一帧的“上一帧”
        std::string reason_;
        if (frame.image_.empty() || frame.image_.dims != 2 || frame.image_.type() != CV_8UC3 ||
            !validate_frame_stamp(stamp, impl_->last_input_, reason_))
        {
            reset();
            result.diagnostics_.push_back(reason_.empty() ? "INPUT_FORMAT" : reason_);
            return result;
        }  // 空图，没像素；不是二维图；不是 8 位三通道彩色图（BGR）；帧号 / 时间戳乱序（重复帧、倒退）
        result = run_decode_pipeline(frame, impl_->config_); // 调管线总调度，返回结果！

        // 中间 result = run_decode_pipeline(...) 赋值，开头两行被整个覆盖（里面是默认构造）掉了，帧号和时间戳变回 0；重写帧身份
        result.frame_id_ = frame.frame_id_;
        result.timestamp_us_ = frame.timestamp_us_;
        if (result.status_ == Status::INVALID_INPUT)
            reset();   // 管线失败了，清状态
        
        // 把这帧的戳和尺寸存下来，成为下一帧的“上一帧”（帧间连续关系（顺序），由 Detector 维护）
        impl_->last_input_ = stamp;
        // 返回结果（每次的 result 只讲当前这一帧）
        return result;
    }

    // 重置内部状态（noexcept保证不抛错）；校验失败、尺寸变了、管线失败时清状态（清掉之后，下一帧就当没有上一帧（跟第一帧一样），坏帧不会连锁污染）
    void Detector::reset() noexcept
    {
        impl_->last_input_.reset();
    }

} // namespace mark::detector
