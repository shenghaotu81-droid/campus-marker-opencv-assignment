// 单独pose_app; 不改旧main
#include "mark/detector.hpp"
#include "mark/tag_detector.hpp"
#include "config/config.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <opencv2/videoio.hpp>

namespace
{
// argc 是参数个数，argv 是参数数组（parse_cli 就是扫这个数组进map，把 --key value 配成对按key取用）
// 参数解析的具体实现：定允许的参数名（--video、--out 等 7 个）、把 argv 扫进 map、校验（未知参数、重复、缺值都抛错）（main 开头调用）
// 独立 G2 运行器只编排已有 Detector 与新感知层，不修改原 marker_app 模式。
std::map<std::string, std::string> parse_cli(int argc, char **argv)
{
    const std::set<std::string> allowed{"--config", "--detector-config", "--video", "--out",
                                         "--camera-id", "--session-id", "--expected-frames"};
    std::map<std::string, std::string> result;
    for (int i = 1; i < argc; ++i)
    {
        const std::string key = argv[i];
        if (key == "--check-config")
        {
            if (!result.emplace(key, "1").second)
                throw std::runtime_error("重复 --check-config");
            continue;
        }
        if (!allowed.count(key) || i + 1 >= argc ||
            std::string(argv[i + 1]).rfind("--", 0) == 0 ||
            !result.emplace(key, argv[++i]).second)
            throw std::runtime_error("未知参数、重复参数或缺值: " + key);
    }
    if (result.count("--check-config"))
    {
        for (const auto &[key, value] : result)
        {
            (void)value;
            if (key != "--check-config" && key != "--config" && key != "--detector-config")
                throw std::runtime_error("check-config 不能混用运行参数");
        }
        return result;
    }
    for (const auto *key : {"--video", "--out", "--camera-id", "--session-id"})
        if (!result.count(key) || result.at(key).empty())
            throw std::runtime_error(std::string("缺参数: ") + key);
    return result;
}

// 把 PoseObservation 写进 YAML(内容：状态（Unique/Ambiguous…）、r/t、重投影误差、候选列表、拒绝原因)
// observations.yaml:while (capture.read(image))读取一帧写一条{frame_id, mark:[...], tags:[...]}，记录分别的位姿结果；没检出空数组
// YAML 保留全部候选和拒绝证据；不只输出一个矩阵让使用者误认为已经消歧。
void write_pose(cv::FileStorage &fs, const mark::pose::PoseObservation &p)
{
    fs << "{" << "source" << p.source_ << "target_id" << p.target_id_
       << "status" << mark::pose::pose_status_name(p.status_)
       << "detection_index" << std::to_string(p.detection_index_)
       << "control_usable" << static_cast<int>(p.control_usable_) << "reasons" << "[";
    for (const auto &reason : p.reasons_)
        fs << reason;
    fs << "]" << "candidates" << "[";
    for (const auto &candidate : p.candidates_)
    {
        fs << "{" << "rvec" << cv::Mat(candidate.pose_.rvec_)
           << "t_m" << cv::Mat(candidate.pose_.t_m_)
           << "metrics_valid" << static_cast<int>(candidate.metrics_valid_);
        if (candidate.metrics_valid_)
            fs << "rmse_px" << candidate.rmse_px_ << "max_error_px" << candidate.max_error_px_;
        if (std::isfinite(candidate.min_depth_m_))
            fs << "min_depth_m" << candidate.min_depth_m_;
        fs << "eligible" << static_cast<int>(candidate.eligible_) << "reasons" << "[";
        for (const auto &reason : candidate.reasons_)
            fs << reason;
        fs << "]" << "}";
    }
    fs << "]";
    if (p.selected_index_)
        fs << "selected_index" << static_cast<int>(*p.selected_index_);
    if (p.T_camera_target_)
        fs << "rvec" << cv::Mat(p.T_camera_target_->rvec_)
           << "t_m" << cv::Mat(p.T_camera_target_->t_m_);
    if (p.selected_rmse_px_)
        fs << "selected_raw_rmse_px" << *p.selected_rmse_px_;
    if (p.refined_rmse_px_)
        fs << "refined_rmse_px" << *p.refined_rmse_px_;
    fs << "covariance_available" << static_cast<int>(p.covariance_se3_.has_value()) << "}";
}
} // namespace

// 录像时钟明确标为回放，不伪造曝光时刻；所有输出是离线证据，不能批准实车控制。
int main(int argc, char **argv)
{
    try
    {
        // 参数解析（main 开头）：--video、--check-config、--out 等 CLI
        const auto args = parse_cli(argc, argv);
        const auto root = std::filesystem::path(__FILE__).parent_path().parent_path();
        const auto cfg_path = args.count("--config") ? std::filesystem::path(args.at("--config")) :
                                                      root / "config/pose.yaml";
        const auto detector_path = args.count("--detector-config") ?
            std::filesystem::path(args.at("--detector-config")) : root / "config/detector.yaml";
        
        // 加载配置（pose.yaml → G2Config）
        const auto cfg = mark::pose::load_pose_config(cfg_path);
        if (args.count("--check-config"))
        {
            mark::Detector detector(mark::loadConfig(detector_path).detector_config);
            mark::pose::TagDetector tags(cfg.tag_);
            std::cout << "G2 配置格式/模型/标定加载通过；预算未测可为空，"
                         "这不代表位姿或硬件已就绪\n";
            return 0;
        }

        // 建对象（detector、estimator、tags、开视频、开输出 YAML）
        if (args.at("--camera-id") != cfg.camera_.camera_id_)
            throw std::runtime_error("声明相机身份与标定配置不一致");
        auto app = mark::loadConfig(detector_path);
        mark::Detector detector(app.detector_config);
        mark::pose::PoseEstimator estimator(cfg);
        mark::pose::TagDetector tags(cfg.tag_);
        cv::VideoCapture capture(args.at("--video"));
        if (!capture.isOpened())
            throw std::runtime_error("视频打不开");
        const double fps = capture.get(cv::CAP_PROP_FPS);
        if (!std::isfinite(fps) || fps <= 0)
            throw std::runtime_error("录像帧率不可用，不能构造回放时间轴");
        std::optional<std::uint64_t> expected;
        if (args.count("--expected-frames"))
        {
            const auto &token = args.at("--expected-frames");
            if (token.empty() || token.find_first_not_of("0123456789") != std::string::npos)
                throw std::runtime_error("expected-frames 必须为正整数");
            expected = std::stoull(token);
            if (*expected == 0)
                throw std::runtime_error("expected-frames 不能为零");
        }
        const std::filesystem::path out(args.at("--out"));
        if (std::filesystem::exists(out))
            throw std::runtime_error("输出目录必须不存在，避免覆盖旧证据");
        std::filesystem::create_directories(out);
        std::filesystem::copy_file(cfg_path, out / "pose_config_input.yaml");
        std::filesystem::copy_file(detector_path, out / "detector_config_input.yaml");
        cv::FileStorage effective((out / "camera_and_model.yaml").string(), cv::FileStorage::WRITE);  // 运行时的"配置快照"：把实际用的 K、dist、图像尺寸、MARK 模型点原样写一份存档
        if (!effective.isOpened())
            throw std::runtime_error("输出配置无法写入");
        effective << "camera_id" << cfg.camera_.camera_id_ << "K" << cfg.camera_.K_
                  << "dist" << cfg.camera_.dist_ << "width" << cfg.camera_.image_size_.width
                  << "height" << cfg.camera_.image_size_.height << "mark_points_m" << "[";
        for (const auto &p : cfg.mark_points_m_)
            effective << "[" << p.x << p.y << p.z << "]";
        effective << "]" << "hardware_confirmed" << 0;
        effective.release();
        cv::FileStorage fs((out / "observations.yaml").string(), cv::FileStorage::WRITE);  // 每帧的位姿结果
        if (!fs.isOpened())
            throw std::runtime_error("观测文件无法写入");
        fs << "schema_version" << 1 << "session_id" << args.at("--session-id")
           << "camera_id" << cfg.camera_.camera_id_ << "clock_domain" << "video-index-clock"
           << "note" << "未经过实车确认；没有真实曝光时间" << "frames" << "[";
        
        // 初始化（image 矩阵、计数器）
        cv::Mat image;
        std::uint64_t frames = 0, raw_count = 0, unique_count = 0, tag_count = 0;

        // 逐帧读视频主循环：读帧 → 调检测器（检测） → 估计：先mark estimate_mark → 估计：后 Tag estimate_exit → write_pose 写 YAML
        while (capture.read(image))
        {
            if (image.empty() || image.size() != cfg.camera_.image_size_ || image.type() != CV_8UC3)
                throw std::runtime_error("输入尺寸/像素类型与原始标定不一致，禁止隐式缩放 K");
            const long double us = frames * 1e6L / fps;
            if (us > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
                throw std::runtime_error("视频时间轴溢出");
            mark::pose::CaptureStamp stamp_;
            stamp_.session_id_ = args.at("--session-id");
            stamp_.camera_id_ = cfg.camera_.camera_id_;
            stamp_.clock_domain_ = "video-index-clock";
            stamp_.pixel_domain_ = mark::pose::PixelDomain::RawDistorted;
            stamp_.frame_id_ = frames;
            stamp_.received_us_ = static_cast<std::int64_t>(std::llround(us));
            mark::FrameInput input{image, frames, stamp_.received_us_, mark::TimestampSource::Unknown};
            const auto raw = detector.process(input);
            if (raw.status == mark::Status::NOT_READY || raw.status == mark::Status::INVALID_INPUT)
                throw std::runtime_error("原 Detector 未就绪或拒绝输入；先检查其配置/时间序列");
            fs << "{" << "frame_id" << std::to_string(frames)
               << "timestamp_us" << std::to_string(stamp_.received_us_) << "mark" << "[";
            for (std::size_t i = 0; i < raw.detections.size(); ++i)
            {
                auto pose_ = estimator.estimate_mark(raw, i, image.size(), stamp_);
                ++raw_count;
                unique_count += pose_.status_ == mark::pose::PoseStatus::Unique;
                write_pose(fs, pose_);  // MARK 循环
            }
            fs << "]" << "tags" << "[";
            for (const auto &tag_ : tags.detect(image, stamp_))
            {
                ++tag_count;
                const auto exit = mark::pose::estimate_exit(tag_, image.size(), estimator);
                fs << "{" << "id" << tag_.id_ << "hamming" << tag_.hamming_
                   << "decision_margin" << tag_.decision_margin_ << "corners" << "[";
                for (const auto &p : tag_.corners_)
                    fs << "[" << p.x << p.y << "]";
                fs << "]" << "decode_accepted" << static_cast<int>(tag_.decode_accepted_)
                   << "reasons" << "[";
                for (const auto &reason : tag_.reasons_)
                    fs << reason;
                fs << "]" << "pose";
                write_pose(fs, exit.pose_); // Tag 循环
                fs << "}";
            }
            fs << "]" << "}";
            ++frames;
        }
        fs << "]";
        // 收尾
        fs.release(); // 关 observations.yaml

        // 写 summary.yaml：总帧数、MARK 数、唯一位姿数、Tag 数、完整性校验
        const bool complete = expected && frames == *expected;
        cv::FileStorage summary((out / "summary.yaml").string(), cv::FileStorage::WRITE);
        if (!summary.isOpened())
            throw std::runtime_error("摘要无法写入");
        summary << "frames" << std::to_string(frames) << "raw_mark_count" << std::to_string(raw_count)
                << "unique_pose_count" << std::to_string(unique_count)
                << "tag_count" << std::to_string(tag_count)
                << "completeness_verified" << static_cast<int>(complete)
                << "incomplete" << static_cast<int>(expected && !complete)
                << "hardware_confirmed" << 0;
        // 打印统计到终端，返回退出码（0 正常，2 帧数不对或空）
        std::cout << "处理帧数=" << frames << " MARK=" << raw_count << " 唯一位姿=" << unique_count
                  << " Tag=" << tag_count << "；未经过实车确认\n";
        if (!expected)
            std::cout << "完整性未验证：未提供独立期待帧数\n";
        return frames == 0 || (expected && !complete) ? 2 : 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "G2 错误: " << e.what() << '\n';
        return 1;  // 抛异常
    }
}