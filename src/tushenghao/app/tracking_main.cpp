#include "vision/config/cli_options.hpp"
#include "vision/tracking/target_tracker.hpp"
#include "io/tracking_io.hpp"
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <chrono>
#include <algorithm>

namespace
{
// 严格CLI避免拼错参数后静默使用默认；配置未测仍可检查，但不假称有速度。
std::map<std::string, std::string> parse_cli(int argc, char **argv)
{
    const std::set<std::string> valued{"--config", "--profile", "--input", "--out"};
    auto options=mark::parse_options(argc,argv,valued,{"--check-config"},true,
        [](mark::OptionError error,const std::string &key)
        {
            if(error==mark::OptionError::DuplicateFlag)throw std::runtime_error("重复check-config");
            throw std::runtime_error("未知/重复/缺值参数:"+key);
        });
    if (options.count("--check-config"))
    {
        if (options.count("--input") || options.count("--out"))
            throw std::runtime_error("check-config不能带输入输出");
    }
    else if (!options.count("--input") || !options.count("--out"))
        throw std::runtime_error("用法: tracking_app --check-config | --input YAML --out 新目录 [--config YAML] [--profile YAML]");
    return options;
}
} // namespace

// 离线执行器每帧tick，保留全部状态/失败；不改G2 writer，不输出决策或控制。
int main(int argc, char **argv)
{
    try
    {
        const auto options = parse_cli(argc, argv);
        const auto root = std::filesystem::path(__FILE__).parent_path().parent_path();
        const auto config_path = options.count("--config") ? std::filesystem::path(options.at("--config")) :
                                                           root / "config/tracking.yaml";
        auto config = mark::tracking::load_tracking_config(config_path);
        if (options.count("--profile"))
            config.profile_ = mark::tracking::load_tracking_profile(options.at("--profile"));
        mark::tracking::validate_tracking_config(config);
        if (options.count("--check-config"))
        {
            std::cout << "G4配置合法；profile=" << (config.profile_ ? "已加载" : "未测/NOT_READY")
                      << "；不代表实车速度已验证\n";
            return 0;
        }
        if (!config.profile_)
            throw std::runtime_error("G4缺独立标定profile；先运行tracking_lab，不填经验值");
        const auto frames = mark::tracking::read_tracking_frames(options.at("--input"));
        const std::filesystem::path out(options.at("--out"));
        if (std::filesystem::exists(out)) throw std::runtime_error("拒绝覆盖现有输出目录");
        std::filesystem::create_directories(out);
        std::filesystem::copy_file(config_path, out / "tracking_config_input.yaml");
        mark::tracking::write_tracking_profile(*config.profile_, out / "effective_profile.yaml");
        mark::tracking::TargetTracker tracker(config);
        std::vector<mark::tracking::TrackingResult> results;
        std::vector<double> step_us;
        std::size_t measured = 0, coast = 0, lost = 0, rejected = 0, qualified = 0, velocity_frames = 0;
        for (auto frame : frames)
        {
            // 文件回放不能继承内存流的实车资格；保留时间字段，但来源降为离线。
            if (frame.origin_ == mark::tracking::InputOrigin::TypedMemory)
                frame.origin_ = mark::tracking::InputOrigin::Synthetic;
            const auto begin = std::chrono::steady_clock::now();
            auto result = tracker.step(frame);
            step_us.push_back(std::chrono::duration<double, std::micro>(
                std::chrono::steady_clock::now() - begin).count());
            rejected += !result.input_accepted_;
            for (const auto &track : result.tracks_)
            {
                measured += track.state_ == mark::tracking::TrackState::Measured;
                coast += track.state_ == mark::tracking::TrackState::Coasting;
                lost += track.state_ == mark::tracking::TrackState::Lost;
                qualified += track.qualified_for_g5_;
                velocity_frames += track.velocity_mps_.has_value();
            }
            results.push_back(std::move(result));
        }
        mark::tracking::write_tracking_results(results, out / "tracks.yaml");
        cv::FileStorage summary((out / "summary.yaml").string(), cv::FileStorage::WRITE);
        if (!summary.isOpened()) throw std::runtime_error("摘要写入失败");
        double total_us = 0;
        for (double value : step_us) total_us += value;
        std::sort(step_us.begin(), step_us.end());
        summary << "frames" << std::to_string(frames.size()) << "measured" << std::to_string(measured)
                << "coasting" << std::to_string(coast) << "lost" << std::to_string(lost)
                << "rejected_inputs" << std::to_string(rejected) << "qualified_for_g5" << std::to_string(qualified)
                << "velocity_output_frames" << std::to_string(velocity_frames)
                << "source_completeness_verified" << 0
                << "hardware_confirmed" << 0 << "input_path" << options.at("--input")
                << "tracking_step_mean_us" << total_us / step_us.size()
                << "tracking_step_p95_us" << step_us[(step_us.size() * 95 + 99) / 100 - 1]
                << "note" << "未经过实车确认；零测量不算端到端速度通过";
        std::cout << "G4帧数=" << frames.size() << "，实测更新=" << measured << "，预测=" << coast
                  << "，LOST=" << lost << "，拒绝输入=" << rejected << "；未经过实车确认\n";
        // 零测量保留诊断但非零退出，防止现G2 BudgetMissing链被误写为完成。
        if (measured == 0 || velocity_frames == 0)
        {
            std::cerr << "没有可输出速度，速度链未验收；检查G2 Unique及G4时间/坐标/噪声/关联事件\n";
            return 2;
        }
        return rejected ? 3 : 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "G4错误:" << e.what() << '\n';
        return 1;
    }
}
