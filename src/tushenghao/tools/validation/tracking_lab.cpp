#include "vision/math/covariance_validation.hpp"
#include "vision/tracking/target_tracker.hpp"
#include "filter/kalman_cv.hpp"
#include "measurement/measurement_adapter.hpp"
#include "io/tracking_io.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <opencv2/calib3d.hpp>

namespace
{
namespace fs = std::filesystem;
namespace tr = mark::tracking;
using Vec3 = cv::Vec3d;

// 真值只属于lab评分器，不进入TargetTracker；每个session保留自己的完整时间序列。
struct Truth
{
    std::string session_;
    std::uint64_t frame_{0};
    double time_s_{0};
    Vec3 p_camera_{0, 0, 0};
    Vec3 v_camera_{0, 0, 0};
    Vec3 p_world_{0, 0, 0};
    Vec3 v_world_{0, 0, 0};
    bool true_association_{true};
    bool injected_error_{false};
    bool reversed_{false};
};

struct Session
{
    std::string split_;
    std::string name_;
    fs::path frames_;
    fs::path truth_;
};

// 机械读配置不提供经验fallback；缺字段、错类型和非有限值均明确失败。
cv::FileNode required(const cv::FileNode &node, const std::string &key)
{
    const auto value = node[key];
    if (value.empty())
        throw std::runtime_error("lab 缺配置: " + key);
    return value;
}

double number(const cv::FileNode &node, const std::string &key)
{
    const auto value = required(node, key);
    if (!value.isReal() && !value.isInt())
        throw std::runtime_error("lab 应为数值: " + key);
    const double result = static_cast<double>(value);
    if (!std::isfinite(result))
        throw std::runtime_error("lab 非有限数: " + key);
    return result;
}

std::string string_value(const cv::FileNode &node, const std::string &key)
{
    const auto value = required(node, key);
    if (!value.isString())
        throw std::runtime_error("lab 应为字符串: " + key);
    return static_cast<std::string>(value);
}

Vec3 vector_value(const cv::FileNode &node, const std::string &key)
{
    const auto value = required(node, key);
    if (!value.isSeq() || value.size() != 3)
        throw std::runtime_error("lab 向量必须三维: " + key);
    Vec3 result;
    for (int i = 0; i < 3; ++i)
    {
        if (!value[i].isReal() && !value[i].isInt())
            throw std::runtime_error("lab 向量含非数值: " + key);
        result[i] = static_cast<double>(value[i]);
    }
    if (!tr::detail::finite_vector(result))
        throw std::runtime_error("lab 向量非有限: " + key);
    return result;
}

// 固定语义的结构属性不是阈值；所有场景数值都读自YAML并以假设身份写入证据。
struct LabConfig
{
    cv::FileStorage storage_;
    explicit LabConfig(const fs::path &path)
        : storage_(path.string(), cv::FileStorage::READ)
    {
        if (!storage_.isOpened() || number(storage_.root(), "schema_version") != 1)
            throw std::runtime_error("lab 文件打不开或版本不支持");
        if ((text("tracking_coordinate_mode") != "camera_relative" &&
             text("tracking_coordinate_mode") != "local_world") ||
            (text("tracking_time_policy") != "nominal_research" &&
             text("tracking_time_policy") != "bounded_only"))
            throw std::runtime_error("lab tracking_coordinate_mode/time_policy非法");
        for (const auto &key : {"duration_s", "nominal_dt_s", "acceleration_mps2",
                                "segment_min_s", "segment_max_s", "synthetic_time_bound_s"})
            if (get(key) <= 0)
                throw std::runtime_error(std::string("lab 必须正数: ") + key);
        for (const auto &key : {"dt_jitter_fraction", "stop_probability", "random_missing_probability",
                                "ambiguous_probability", "budget_missing_probability", "injected_wrong_probability"})
            if (get(key) < 0 || get(key) >= 1)
                throw std::runtime_error(std::string("lab 概率/比例须在[0,1): ") + key);
        if (get("segment_max_s") < get("segment_min_s") ||
            get("rule_reference_speed_mps") <= 0 || get("occlusion_start_s") < 0 ||
            get("occlusion_duration_s") < 0 || get("baseline_reference_dt_s") <= 0 ||
            get("baseline_reference_alpha") <= 0 || get("baseline_reference_alpha") >= 1)
            throw std::runtime_error("lab 区间/基线配置非法");
        if (get("ambiguous_probability") + get("budget_missing_probability") > 1)
            throw std::runtime_error("lab 不同状态概率之和不可超过1");
        for (const auto &key : {"camera_translation_amplitude_m", "camera_translation_frequency_hz",
                                "camera_yaw_amplitude_rad", "camera_yaw_frequency_hz"})
            if (get(key) < 0)
                throw std::runtime_error(std::string("lab 相机假设须非负: ") + key);
        std::vector<std::pair<double, double>> seed_ranges;
        for (const auto &split : {"train", "selection", "holdout"})
        {
            const double count = get(std::string(split) + "_sessions");
            if (count < 1 || count != std::floor(count))
                throw std::runtime_error("lab session数须为正整数");
            const double seed = get(std::string(split) + "_seed");
            if (seed < 0 || seed != std::floor(seed) ||
                seed + count - 1 > std::numeric_limits<std::uint32_t>::max())
                throw std::runtime_error("lab seed必须为uint32范围内整数");
            for (const auto &range : seed_ranges)
                if (std::max(seed, range.first) <= std::min(seed + count - 1, range.second))
                    throw std::runtime_error("lab train/selection/holdout种子范围重叠，拒绝数据泄漏");
            seed_ranges.emplace_back(seed, seed + count - 1);
        }
        const auto sigma = vector_value(storage_.root(), "position_noise_std_m");
        for (double value : sigma.val)
            if (!(value > 0))
                throw std::runtime_error("lab 位置噪声标准差须严格正，禁止退化R");
    }
    double get(const std::string &key) const { return number(storage_.root(), key); }
    std::string text(const std::string &key) const { return string_value(storage_.root(), key); }
    Vec3 vec(const std::string &key) const { return vector_value(storage_.root(), key); }
};

// 路径/选项集中校验，拒绝未知与重复；输出只允许新目录，不覆盖既有证据。
std::map<std::string, std::string> parse_cli(int argc, char **argv)
{
    std::map<std::string, std::string> result;
    if (argc < 2)
        throw std::runtime_error("用法: tracking_lab generate|calibrate|verify --config YAML --out NEW_DIR [--input DATASET] [--profile PROFILE]");
    result["command"] = argv[1];
    if (result["command"] != "generate" && result["command"] != "calibrate" && result["command"] != "verify")
        throw std::runtime_error("lab 子命令仅generate/calibrate/verify");
    for (int i = 2; i < argc; ++i)
    {
        const std::string key = argv[i];
        if ((key != "--config" && key != "--out" && key != "--input" && key != "--profile") ||
            i + 1 >= argc || std::string(argv[i + 1]).rfind("--", 0) == 0 ||
            !result.emplace(key, argv[++i]).second)
            throw std::runtime_error("lab 参数未知/缺值/重复: " + key);
    }
    if (!result.count("--out") || (result["command"] != "generate" && !result.count("--input")) ||
        (result["command"] == "verify" && !result.count("--profile")))
        throw std::runtime_error("lab 缺--out/--input/--profile");
    if (result["command"] == "generate" && (result.count("--input") || result.count("--profile")))
        throw std::runtime_error("lab generate不接收--input/--profile，避免静默忽略参数");
    return result;
}

void new_directory(const fs::path &path)
{
    if (fs::exists(path))
        throw std::runtime_error("lab 拒绝覆盖输出: " + path.string());
    fs::create_directories(path);
}

// CSV采用无逗号session名；对齐frame与truth的检查在读入后执行，不能靠行数猜。
void write_truth(const std::vector<Truth> &truth, const fs::path &path)
{
    std::ofstream out(path);
    if (!out)
        throw std::runtime_error("lab truth文件写不开");
    out << "session,frame,time_s,px,py,pz,vx,vy,vz,world_px,world_py,world_pz,world_vx,world_vy,world_vz,true_association,injected_error,reversed\n";
    out << std::setprecision(17);
    for (const auto &t : truth)
    {
        out << t.session_ << ',' << t.frame_ << ',' << t.time_s_;
        for (const auto &value : {t.p_camera_, t.v_camera_, t.p_world_, t.v_world_})
            for (double x : value.val)
                out << ',' << x;
        out << ',' << t.true_association_ << ',' << t.injected_error_ << ',' << t.reversed_ << '\n';
    }
    if (!out.good())
        throw std::runtime_error("lab truth文件写入失败");
}

std::vector<Truth> read_truth(const fs::path &path)
{
    std::ifstream in(path);
    std::string line;
    if (!in || !std::getline(in, line))
        throw std::runtime_error("lab truth文件打不开/为空");
    std::vector<Truth> result;
    while (std::getline(in, line))
    {
        if (line.empty())
            continue;
        std::vector<std::string> cells;
        std::stringstream stream(line);
        std::string cell;
        while (std::getline(stream, cell, ','))
            cells.push_back(cell);
        if (cells.size() != 18)
            throw std::runtime_error("lab truth列数错误");
        Truth t;
        t.session_ = cells[0];
        t.frame_ = std::stoull(cells[1]);
        t.time_s_ = std::stod(cells[2]);
        for (int i = 0; i < 3; ++i)
        {
            t.p_camera_[i] = std::stod(cells[3 + i]);
            t.v_camera_[i] = std::stod(cells[6 + i]);
            t.p_world_[i] = std::stod(cells[9 + i]);
            t.v_world_[i] = std::stod(cells[12 + i]);
        }
        t.true_association_ = std::stoi(cells[15]) != 0;
        t.injected_error_ = std::stoi(cells[16]) != 0;
        t.reversed_ = std::stoi(cells[17]) != 0;
        if (!std::isfinite(t.time_s_) || !tr::detail::finite_vector(t.p_camera_) ||
            !tr::detail::finite_vector(t.v_camera_))
            throw std::runtime_error("lab truth含非有限值");
        result.push_back(t);
    }
    return result;
}

// manifest固定存相对路径，整个数据集移动后仍可读；不在源码嵌机器目录。
std::vector<Session> read_manifest(const fs::path &root)
{
    cv::FileStorage input((root / "manifest.yaml").string(), cv::FileStorage::READ);
    if (!input.isOpened() || number(input.root(), "schema_version") != 1 || !input["sessions"].isSeq())
        throw std::runtime_error("lab 数据集manifest无效");
    std::vector<Session> result;
    std::set<std::string> names, frame_paths;
    for (const auto &entry : input["sessions"])
    {
        Session s;
        s.split_ = string_value(entry, "split");
        s.name_ = string_value(entry, "name");
        const fs::path frame_path(string_value(entry, "frames"));
        const fs::path truth_path(string_value(entry, "truth"));
        if ((s.split_ != "train" && s.split_ != "selection" && s.split_ != "holdout") ||
            s.name_.empty() || s.name_.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != std::string::npos ||
            !names.insert(s.name_).second || !frame_paths.insert(frame_path.generic_string()).second)
            throw std::runtime_error("lab session/split重复或非法，拒绝重复数据");
        for (const auto &path : {frame_path, truth_path})
        {
            if (path.empty() || path.is_absolute())
                throw std::runtime_error("lab manifest资源必须为相对路径");
            for (const auto &component : path)
                if (component == "..")
                    throw std::runtime_error("lab manifest资源不能逃出数据集");
        }
        s.frames_ = root / frame_path;
        s.truth_ = root / truth_path;
        result.push_back(s);
    }
    return result;
}

void aligned(const std::vector<tr::TrackingFrame> &frames, const std::vector<Truth> &truth)
{
    if (frames.empty() || frames.size() != truth.size())
        throw std::runtime_error("lab 帧与truth为空/数量不等");
    for (std::size_t i = 0; i < frames.size(); ++i)
        if (frames[i].stamp_.session_id_ != truth[i].session_ ||
            frames[i].stamp_.frame_id_ != truth[i].frame_)
            throw std::runtime_error("lab frame/session与truth不对齐");
}

// 数据生成器完整模拟“框世界运动+相机运动”；相机变化率真值含本车平移与旋转。
void generate_session(const LabConfig &cfg, std::uint32_t seed, const std::string &name,
                      const fs::path &directory)
{
    fs::create_directories(directory);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> uniform(0, 1);
    std::normal_distribution<double> normal(0, 1);
    std::vector<tr::TrackingFrame> frames;
    std::vector<Truth> truth;
    Vec3 p_world = cfg.vec("initial_world_position_m"), v_world(0, 0, 0);
    const auto sigma = cfg.vec("position_noise_std_m"), bias = cfg.vec("position_bias_m");
    double time = 0, remaining = 0, desired = cfg.get("rule_reference_speed_mps");
    std::int64_t time_us = 0;
    double previous_nonzero_world_vx = 0;
    while (time <= cfg.get("duration_s"))
    {
        const double raw_dt = cfg.get("nominal_dt_s") *
            (1 + cfg.get("dt_jitter_fraction") * (2 * uniform(rng) - 1));
        const long double proposed_us = static_cast<long double>(time_us) + raw_dt * 1e6L;
        if (!std::isfinite(proposed_us) || proposed_us > std::numeric_limits<std::int64_t>::max())
            throw std::runtime_error("lab 合成时间轴溢出");
        const auto next_us = static_cast<std::int64_t>(std::llround(proposed_us));
        if (next_us <= time_us)
            throw std::runtime_error("lab dt小于整数us分辨率，不能生成重复时刻");
        if (static_cast<long double>(next_us) / 1e6L > cfg.get("duration_s"))
            break;
        // 真合成时钟先落定到整数us，再用同一dt积分；不让四舍五入误差超出声明时间界。
        const double dt = static_cast<double>(next_us - time_us) / 1e6;
        if (remaining <= 0)
        {
            desired = uniform(rng) < cfg.get("stop_probability") ? 0 :
                -std::copysign(cfg.get("rule_reference_speed_mps"), desired == 0 ? 1.0 : desired);
            remaining = cfg.get("segment_min_s") + uniform(rng) *
                (cfg.get("segment_max_s") - cfg.get("segment_min_s"));
        }
        const auto old_v = v_world;
        v_world[0] += std::clamp(desired - v_world[0],
            -cfg.get("acceleration_mps2") * dt, cfg.get("acceleration_mps2") * dt);
        p_world += (old_v + v_world) * (dt / 2);
        remaining -= dt;
        // 状态积分与记录时刻必须一起推进，不能用下一随机dt积分却标上上一帧时间。
        time_us = next_us;
        time = static_cast<double>(time_us) / 1e6;
        const double translation_omega = 2 * CV_PI * cfg.get("camera_translation_frequency_hz");
        const double rotation_omega = 2 * CV_PI * cfg.get("camera_yaw_frequency_hz");
        const double cx = cfg.get("camera_translation_amplitude_m") * std::sin(translation_omega * time);
        const double cvx = cfg.get("camera_translation_amplitude_m") * translation_omega * std::cos(translation_omega * time);
        const double yaw = cfg.get("camera_yaw_amplitude_rad") * std::sin(rotation_omega * time);
        const double yaw_rate = cfg.get("camera_yaw_amplitude_rad") * rotation_omega * std::cos(rotation_omega * time);
        cv::Matx33d R_world_camera;
        cv::Rodrigues(Vec3(0, yaw, 0), R_world_camera);
        const Vec3 t_world_camera(cx, 0, 0);
        const Vec3 p_camera = R_world_camera.t() * (p_world - t_world_camera);
        const Vec3 v_camera = R_world_camera.t() * (v_world - Vec3(cvx, 0, 0)) -
                              Vec3(0, yaw_rate, 0).cross(p_camera);
        tr::TrackingFrame frame;
        frame.origin_ = tr::InputOrigin::Synthetic;
        frame.stamp_.session_id_ = name;
        frame.stamp_.camera_id_ = cfg.text("camera_id");
        frame.stamp_.clock_domain_ = "synthetic-exposure-clock";
        frame.stamp_.pixel_domain_ = mark::pose::PixelDomain::RawDistorted;
        frame.stamp_.frame_id_ = frames.size();
        frame.stamp_.exposure_us_ = time_us;
        frame.stamp_.received_us_ = *frame.stamp_.exposure_us_;
        frame.stamp_.time_uncertainty_s_ = cfg.get("synthetic_time_bound_s") * uniform(rng);
        tr::CameraTransform transform;
        transform.stamp_ = frame.stamp_;
        transform.world_frame_id_ = "synthetic-world";
        transform.R_world_camera_ = R_world_camera;
        transform.t_world_camera_m_ = t_world_camera;
        transform.covariance_ = tr::Mat6::zeros(); // 明确真值仿真中的精确变换，不代表硬件零误差。
        frame.camera_transform_ = transform;
        Truth t;
        t.session_ = name;
        t.frame_ = frame.stamp_.frame_id_;
        t.time_s_ = time;
        t.p_camera_ = p_camera;
        t.v_camera_ = v_camera;
        t.p_world_ = p_world;
        t.v_world_ = v_world;
        t.reversed_ = previous_nonzero_world_vx != 0 && v_world[0] != 0 &&
                      previous_nonzero_world_vx * v_world[0] < 0;
        if (v_world[0] != 0)
            previous_nonzero_world_vx = v_world[0];
        const bool occluded = time >= cfg.get("occlusion_start_s") &&
            time < cfg.get("occlusion_start_s") + cfg.get("occlusion_duration_s");
        if (!occluded && uniform(rng) >= cfg.get("random_missing_probability"))
        {
            mark::pose::PoseObservation observation;
            observation.stamp_ = frame.stamp_;
            observation.source_ = cfg.text("source");
            observation.target_id_ = cfg.text("target_class");
            observation.status_ = mark::pose::PoseStatus::Unique;
            mark::pose::RigidPose pose;
            cv::Rodrigues(R_world_camera.t(), pose.rvec_);
            pose.t_m_ = p_camera + bias;
            for (int axis = 0; axis < 3; ++axis)
                pose.t_m_[axis] += sigma[axis] * normal(rng);
            if (uniform(rng) < cfg.get("injected_wrong_probability"))
            {
                pose.t_m_ += cfg.vec("injected_offset_m");
                t.true_association_ = false;
                t.injected_error_ = true;
            }
            observation.T_camera_target_ = pose;
            observation.selected_index_ = 0;
            mark::pose::PoseCandidate candidate;
            candidate.pose_ = pose;
            // 这是绕过G2的点目标夹具，需补齐所声明的正深度证据；不是实图四角验收。
            candidate.min_depth_m_ = pose.t_m_[2];
            candidate.metrics_valid_ = true;
            candidate.eligible_ = true;
            observation.candidates_.push_back(candidate);
            const double status = uniform(rng);
            if (status < cfg.get("ambiguous_probability"))
                observation.status_ = mark::pose::PoseStatus::Ambiguous;
            else if (status < cfg.get("ambiguous_probability") + cfg.get("budget_missing_probability"))
                observation.status_ = mark::pose::PoseStatus::BudgetMissing;
            if (observation.status_ != mark::pose::PoseStatus::Unique)
            {
                observation.T_camera_target_.reset();
                observation.selected_index_.reset();
            }
            frame.observations_.push_back(std::move(observation));
        }
        frames.push_back(std::move(frame));
        truth.push_back(t);
    }
    tr::write_tracking_frames(frames, directory / "frames.yaml");
    write_truth(truth, directory / "truth.csv");
}

// 先按完整session分集；种子各自独立，manifest保留来源而非让verify重新抽样。
void generate(const LabConfig &cfg, const fs::path &out)
{
    cv::FileStorage manifest((out / "manifest.yaml").string(), cv::FileStorage::WRITE);
    if (!manifest.isOpened())
        throw std::runtime_error("lab manifest写不开");
    manifest << "schema_version" << 1 << "basis" << "synthetic_assumptions_bypass_G2"
             << "hardware_verified" << 0
             << "time_model" << "integer_us_exact_clock_declared_bounds_no_exposure_error_injection"
             << "sessions" << "[";
    for (const auto &split : {"train", "selection", "holdout"})
        for (int i = 0; i < cfg.get(std::string(split) + "_sessions"); ++i)
        {
            const auto seed = static_cast<std::uint32_t>(cfg.get(std::string(split) + "_seed")) + i;
            const std::string name = std::string(split) + "-seed-" + std::to_string(seed);
            const fs::path relative = fs::path(split) / name;
            generate_session(cfg, seed, name, out / relative);
            manifest << "{" << "split" << split << "name" << name
                     << "frames" << (relative / "frames.yaml").generic_string()
                     << "truth" << (relative / "truth.csv").generic_string() << "}";
        }
    manifest << "]";
}

// 样本标准差用n-1；没有足够样本就报错，不用经验floor造出一个可用profile。
struct Statistics
{
    std::vector<double> samples_;
    void add(double value)
    {
        if (!std::isfinite(value))
            throw std::runtime_error("lab 统计输入非有限");
        samples_.push_back(value);
    }
    double mean() const
    {
        if (samples_.size() < 2)
            throw std::runtime_error("lab 统计不足两个样本");
        return std::accumulate(samples_.begin(), samples_.end(), 0.0) / samples_.size();
    }
    double stddev() const
    {
        const double mu = mean();
        double sum = 0;
        for (double x : samples_)
            sum += (x - mu) * (x - mu);
        return std::sqrt(sum / (samples_.size() - 1));
    }
    double limit() const { return mean() + 3 * stddev(); }
};

bool current_unique(const tr::TrackingFrame &f)
{
    return f.observations_.size() == 1 &&
        f.observations_[0].status_ == mark::pose::PoseStatus::Unique &&
        f.observations_[0].T_camera_target_.has_value();
}

// 与生产适配器使用同一坐标/时间政策；partial profile只用于无gate的离线统计。
tr::TrackingConfig tracker_config(const LabConfig &cfg, const tr::TrackingProfile &profile)
{
    tr::TrackingConfig result;
    result.source_ = cfg.text("source");
    result.target_class_ = cfg.text("target_class");
    result.coordinate_mode_ = cfg.text("tracking_coordinate_mode") == "local_world" ?
        tr::CoordinateMode::LocalWorld : tr::CoordinateMode::CameraRelative;
    result.world_frame_id_ = "synthetic-world";
    result.time_policy_ = cfg.text("tracking_time_policy");
    result.stamp_uncertainty_semantics_ = "absolute_bound";
    result.max_time_uncertainty_s_ = profile.validated_time_bound_s_;
    result.profile_ = profile;
    return result;
}

Vec3 truth_position(const Truth &truth, tr::CoordinateMode mode)
{
    return mode == tr::CoordinateMode::LocalWorld ? truth.p_world_ : truth.p_camera_;
}

Vec3 truth_velocity(const Truth &truth, tr::CoordinateMode mode)
{
    return mode == tr::CoordinateMode::LocalWorld ? truth.v_world_ : truth.v_camera_;
}

// 公共滤波状态顺序已固定[p,v]；这里只取vv块，不把6D矩阵重新解释为其他排列。
cv::Matx33d velocity_block(const tr::Mat6 &covariance)
{
    cv::Matx33d result;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            result(i, j) = covariance(i + 3, j + 3);
    return result;
}

// 绝对界与sigma不同；复用生产helper建立相同的保守时间MSE口径，不重复发明公式。
double frame_bound(const tr::TrackingFrame &frame, bool bounded)
{
    if (!bounded)
        return 0;
    if (!frame.stamp_.exposure_us_ || !frame.stamp_.time_uncertainty_s_ ||
        !std::isfinite(*frame.stamp_.time_uncertainty_s_) || *frame.stamp_.time_uncertainty_s_ < 0)
        throw std::runtime_error("lab bounded_only缺有效曝光时间/绝对时间界");
    return *frame.stamp_.time_uncertainty_s_;
}

// 时间差用实际输入stamp，不用truth时刻帮助参考滤波器获得生产中不存在的时钟。
double input_time(const tr::TrackingFrame &frame)
{
    return static_cast<double>(tr::detail::sample_time(frame.stamp_)) / 1e6;
}

// R/Q只用train，固定后在selection无gate截断地收门控统计；holdout完全不参与定值。
void calibrate(const LabConfig &cfg, const fs::path &input, const fs::path &out,
               const fs::path &profile_path)
{
    const auto sessions = read_manifest(input);
    const auto coordinate_mode = cfg.text("tracking_coordinate_mode") == "local_world" ?
        tr::CoordinateMode::LocalWorld : tr::CoordinateMode::CameraRelative;
    const bool bounded = cfg.text("tracking_time_policy") == "bounded_only";
    cv::Matx33d error_second = cv::Matx33d::zeros();
    Vec3 error_mean(0, 0, 0);
    double delta_v_second = 0, total_dt = 0, speed_second = 0;
    std::size_t errors = 0, velocities = 0;
    Statistics dt_stats, time_stats;
    for (const auto &session : sessions)
    {
        if (session.split_ != "train")
            continue;
        const auto frames = tr::read_tracking_frames(session.frames_);
        const auto truth = read_truth(session.truth_);
        aligned(frames, truth);
        for (std::size_t i = 0; i < frames.size(); ++i)
        {
            const auto true_velocity = truth_velocity(truth[i], coordinate_mode);
            speed_second += true_velocity.dot(true_velocity);
            ++velocities;
            if (i)
            {
                const double dt = truth[i].time_s_ - truth[i - 1].time_s_;
                if (!(dt > 0))
                    throw std::runtime_error("lab truth时间不严格递增");
                const auto dv = true_velocity - truth_velocity(truth[i - 1], coordinate_mode);
                delta_v_second += dv.dot(dv);
                total_dt += dt;
            }
            if (!current_unique(frames[i]) || !truth[i].true_association_)
                continue;
            const auto e = frames[i].observations_[0].T_camera_target_->t_m_ - truth[i].p_camera_;
            error_second += e * e.t();
            error_mean += e;
            ++errors;
        }
    }
    if (!errors || !velocities || !(total_dt > 0))
        throw std::runtime_error("lab train有效样本不足");
    tr::TrackingProfile profile;
    profile.camera_id_ = cfg.text("camera_id");
    profile.camera_mse_m2_ = error_second * (1.0 / errors);
    // 单轴往复不能把能量除3平均到三轴；用trace*I包络各方向二阶矩，避免主轴低估。
    // 这是等方参考模型的保守定义，并不声称真实随机反转满足白加速度假设。
    profile.acceleration_psd_m2ps3_ = delta_v_second / total_dt;
    profile.initial_velocity_variance_m2ps2_ = speed_second / velocities;
    profile.coordinate_mode_ = cfg.text("tracking_coordinate_mode");
    profile.time_policy_ = cfg.text("tracking_time_policy");
    if (!tr::detail::positive_definite(cv::Mat(profile.camera_mse_m2_)) ||
        !(profile.acceleration_psd_m2ps3_ > 0) ||
        !(profile.initial_velocity_variance_m2ps2_ > 0))
        throw std::runtime_error("lab R非正定/q或初速度方差非正；禁止补经验floor");
    Statistics association, bootstrap, position_std, velocity_std;
    std::map<std::string, std::array<Statistics, 6>> session_statistics;
    // R2需要先有profile单帧时间界；预读同一selection统计，不借用holdout或手填界。
    // 原流程最后才写此值，直接传入bootstrap会访问空optional。
    Statistics position_time_stats;
    for (const auto &session : sessions)
        if (session.split_ == "selection")
            for (const auto &frame : tr::read_tracking_frames(session.frames_))
                if (frame.stamp_.time_uncertainty_s_)
                    position_time_stats.add(*frame.stamp_.time_uncertainty_s_);
    profile.validated_time_bound_s_ = position_time_stats.limit();
    const auto reference_config = tracker_config(cfg, profile);
    for (const auto &session : sessions)
    {
        if (session.split_ != "selection")
            continue;
        const auto frames = tr::read_tracking_frames(session.frames_);
        const auto truth = read_truth(session.truth_);
        aligned(frames, truth);
        tr::detail::CvFilter filter;
        bool initialized = false, confirmed = false;
        auto &stratum = session_statistics[session.name_];
        std::optional<Vec3> previous_observed;
        cv::Matx33d previous_R = cv::Matx33d::zeros();
        double previous_time = 0, last_observed_time = 0, last_observed_bound = 0;
        for (std::size_t i = 0; i < frames.size(); ++i)
        {
            const double now = input_time(frames[i]);
            const double bound = frame_bound(frames[i], bounded);
            if (frames[i].stamp_.time_uncertainty_s_)
                stratum[5].add(*frames[i].stamp_.time_uncertainty_s_);
            if (i)
                stratum[4].add(now - input_time(frames[i - 1]));
            // 阈值与时间适用域也仅用selection；不混train，更不读holdout补宽。
            if (frames[i].stamp_.time_uncertainty_s_)
                time_stats.add(*frames[i].stamp_.time_uncertainty_s_);
            if (i)
                dt_stats.add(now - input_time(frames[i - 1]));
            if (initialized)
            {
                const double dt = now - previous_time;
                const double interval_bound = bounded ?
                    bound + frame_bound(frames[i - 1], true) : 0;
                filter.predict(dt, profile.acceleration_psd_m2ps3_, interval_bound);
                position_std.add(filter.position_std());
                velocity_std.add(filter.velocity_std());
                stratum[2].add(filter.position_std());
                stratum[3].add(filter.velocity_std());
            }
            previous_time = now;
            std::vector<std::string> adapter_events;
            const auto observations = tr::detail::adapt_measurements(frames[i], reference_config, adapter_events);
            // 只在离线selection用true_association排除故意注入负例；生产不读取该真值标签。
            if (observations.size() != 1 || !truth[i].true_association_)
                continue;
            const auto observed = observations.front().position_m_;
            auto current_R = observations.front().R_m2_;
            if (bounded)
                current_R = tr::detail::time_augmented_noise(current_R,
                    initialized ? filter.velocity() : Vec3(0, 0, 0),
                    initialized ? velocity_block(filter.covariance()) :
                        cv::Matx33d::eye() * profile.initial_velocity_variance_m2ps2_,
                    bound, profile.acceleration_psd_m2ps3_);
            if (previous_observed)
            {
                tr::detail::CvFilter seed;
                seed.initialize(*previous_observed, previous_R,
                                 profile.initial_velocity_variance_m2ps2_);
                seed.predict(now - last_observed_time, profile.acceleration_psd_m2ps3_,
                             bounded ? bound + last_observed_bound : 0);
                const double bootstrap_score = seed.nis(observed, current_R);
                bootstrap.add(bootstrap_score);
                stratum[1].add(bootstrap_score);
            }
            if (!initialized)
            {
                filter.initialize(observed, current_R,
                                  profile.initial_velocity_variance_m2ps2_);
                initialized = true;
            }
            else if (!confirmed)
            {
                // 第二个真Unique与生产一样用两点bootstrap，不用零速先验连续correct替代。
                filter.bootstrap(*previous_observed, previous_R, observed, current_R,
                    now - last_observed_time, profile.acceleration_psd_m2ps3_,
                    bounded ? bound + last_observed_bound : 0,
                    bounded ? *profile.validated_time_bound_s_ : 0);
                confirmed = true;
            }
            else
            {
                // 创新先记录再correct；没有gate截尾，也不会为报告好看丢弃反转残差。
                const double association_score = filter.nis(observed, current_R);
                association.add(association_score);
                stratum[0].add(association_score);
                filter.correct(observed, current_R);
            }
            previous_observed = observed;
            previous_R = current_R;
            last_observed_time = now;
            last_observed_bound = bound;
        }
    }
    profile.association_nis_limit_ = association.limit();
    profile.bootstrap_nis_limit_ = bootstrap.limit();
    profile.position_std_limit_m_ = position_std.limit();
    profile.velocity_std_limit_mps_ = velocity_std.limit();
    profile.max_dt_s_ = dt_stats.limit();
    profile.validated_time_bound_s_ = time_stats.limit();
    profile.basis_ = "synthetic_3sigma";
    profile.evidence_id_ = "g4lab-train-" + std::to_string(static_cast<std::uint32_t>(cfg.get("train_seed"))) +
        "-selection-" + std::to_string(static_cast<std::uint32_t>(cfg.get("selection_seed"))) +
        "-" + profile.coordinate_mode_ + "-" + profile.time_policy_;
    profile.hardware_verified_ = false;
    tr::validate_tracking_profile(profile);
    if (fs::exists(profile_path))
        throw std::runtime_error("lab 拒绝覆盖已有profile");
    if (!profile_path.parent_path().empty())
        fs::create_directories(profile_path.parent_path());
    tr::write_tracking_profile(profile, profile_path);
    // 为该合成标定域输出可直接回放的研究配置，不触碰默认生产配置或硬件资格。
    {
        const auto relative_profile = fs::relative(profile_path, out);
        if (relative_profile.empty() || relative_profile.is_absolute())
            throw std::runtime_error("lab profile无法相对研究配置定位，请放同一文件系统");
        cv::FileStorage config((out / "tracking_research.yaml").string(), cv::FileStorage::WRITE);
        if (!config.isOpened()) throw std::runtime_error("lab研究配置写失败");
        config.writeComment("仅匹配本次合成条件；未经过实车确认，禁止冒充硬件预算");
        config << "schema_version" << 1 << "source" << cfg.text("source")
               << "target_class" << cfg.text("target_class") << "coordinate_mode" << profile.coordinate_mode_
               << "world_frame_id" << "synthetic-world" << "time_policy" << profile.time_policy_
               << "stamp_uncertainty_semantics" << "absolute_bound" << "max_time_uncertainty_s";
        if (profile.validated_time_bound_s_) config << *profile.validated_time_bound_s_;
        else config << "";
        config << "profile" << relative_profile.generic_string();
    }
    std::ofstream report(out / "calibration.md");
    report << "# G4合成标定记录\n\n未经过实车确认。完整session先分train/selection/holdout；"
              "R/Q/初速度方差仅train，阈值仅selection，holdout只验证。"
              "R为误差二阶矩而非去均值协方差，单独报告系统偏差；固定偏差具有跨帧相关性，"
              "不能仅凭该R证明长期协方差覆盖。q与初速度方差是二阶矩，不是3sigma阈值。\n\n"
              "|统计量|样本数|均值|样本标准差|最终阈值(mu+3sigma)|来源|\n|---|---:|---:|---:|---:|---|\n";
    report << std::setprecision(17);
    for (const auto &entry : std::vector<std::pair<std::string, const Statistics *>>{
             {"association_nis", &association}, {"bootstrap_nis", &bootstrap},
             {"position_std_m", &position_std}, {"velocity_std_mps", &velocity_std},
             {"max_dt_s", &dt_stats}, {"time_bound_reference_s", &time_stats}})
        report << '|' << entry.first << '|' << entry.second->samples_.size() << '|'
               << entry.second->mean() << '|' << entry.second->stddev() << '|'
               << entry.second->limit() << "|" << profile.evidence_id_ << "|\n";
    report << "\nR样本=" << errors << "，bias=[" << error_mean[0] / errors << ','
           << error_mean[1] / errors << ',' << error_mean[2] / errors << "]m；q="
           << profile.acceleration_psd_m2ps3_ << "m²/s³；初速度方差="
           << profile.initial_velocity_variance_m2ps2_ << "m²/s²。\n\n"
              "等方q=Σ||Δv||²/Σdt，初速variance=E||v||²；用trace*I包络避免单轴运动除3低估主轴。"
              "代价是其他轴的预测协方差可能更大、平滑收益可能下降，由独立holdout如实比较，"
              "不据此宣称真实过程是白加速度。\n\n"
              "NIS不是正态，mu+3sigma不代表固定99.7%覆盖。位置/速度std门槛是模型适用域，"
              "不是G5毫米物理容差；时间界只是合成数据支持范围，不能冒充真实设备界。"
              "全部创新在定gate前收集，没有先gate掉大误差再标定。实车参数不得直接复制本profile。\n";
    report << "\n坐标域=" << profile.coordinate_mode_ << "；时间政策=" << profile.time_policy_
           << "。R始终以相机误差定值；q/初速度方差采用该坐标域truth；selection复用生产适配与时间MSE helper。"
              "world模式只验证给定精确ego变换的假设，仍不是实物外参/里程计验证。"
              "合成时钟为精确整数us，时间上界是声明的保守输入；本实验没有注入真实曝光偏移。\n";
    report << "\n## 按完整session分层检查\n\n同一假设域下总体门槛采用上表mu+3sigma；"
              "以下逐层保留明细，便于发现混池掩盖的尾部，不将不同硬件/噪声域直接合并。\n\n"
              "|session|指标|n|mu|sample_std|mu+3sigma|\n|---|---|---:|---:|---:|---:|\n";
    const std::array<const char *, 6> stat_names{{"association_nis", "bootstrap_nis", "position_std_m",
                                                  "velocity_std_mps", "dt_s", "time_bound_s"}};
    for (const auto &[name, values] : session_statistics)
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            report << '|' << name << '|' << stat_names[i] << '|' << values[i].samples_.size() << '|';
            if (values[i].samples_.size() >= 2)
                report << values[i].mean() << '|' << values[i].stddev() << '|' << values[i].limit();
            else
                report << "不足|不足|不足";
            report << "|\n";
        }
    if (!report.good())
        throw std::runtime_error("lab 标定报告写失败");
}

// 对照输入只来自当前测量；truth只进入add_error，不参与任何算法选测量/校正。
struct Metric
{
    std::size_t count_{0};
    double p_sq_{0}, v_sq_{0}, max_v_{0};
    void add(const Vec3 &p, const Vec3 &v, const Truth &truth)
    {
        const double pe = cv::norm(p - truth.p_camera_), ve = cv::norm(v - truth.v_camera_);
        p_sq_ += pe * pe;
        v_sq_ += ve * ve;
        max_v_ = std::max(max_v_, ve);
        ++count_;
    }
};

// 经验包含率按最大特征值3sigma球计数；它不是固定概率保证，也不反向改门槛。
double three_sigma_radius(const tr::Mat6 &covariance, int start)
{
    cv::Matx33d block;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            block(i, j) = covariance(i + start, j + start);
    cv::Mat values;
    if (!cv::eigen(cv::Mat(block), values) || values.at<double>(0) < 0)
        throw std::runtime_error("lab covariance块非法，不能计算包含率");
    return 3 * std::sqrt(values.at<double>(0));
}

// 独立holdout固定运行；报告失败/覆盖/误关联，绝不在verify中重定profile。
void verify(const LabConfig &cfg, const fs::path &input, const fs::path &out,
            const fs::path &profile_path)
{
    const auto profile = tr::load_tracking_profile(profile_path);
    if (profile.camera_id_ != cfg.text("camera_id"))
        throw std::runtime_error("lab profile相机数据身份不匹配");
    if (profile.coordinate_mode_ != cfg.text("tracking_coordinate_mode") ||
        profile.time_policy_ != cfg.text("tracking_time_policy"))
        throw std::runtime_error("lab profile坐标/时间域不匹配，必须在所选域重新calibrate");
    const auto prototype = tracker_config(cfg, profile);
    Metric raw_metric, smooth_metric, tracker_metric, paired_raw, paired_smooth, paired_tracker;
    std::size_t frames_total = 0, wrong_accepted = 0, wrong_confirmed = 0, wrong_injected = 0, qualified = 0;
    std::size_t measured = 0, coast = 0, lost = 0, reversals = 0, recovered = 0, unbounded = 0;
    Statistics recovery;
    // 每状态分别记录[position n, position contained, velocity n, velocity contained]。
    std::map<std::string, std::array<std::size_t, 4>> coverage;
    std::ofstream csv(out / "holdout_errors.csv");
    if (!csv)
        throw std::runtime_error("lab holdout CSV写不开");
    csv << "session,frame,status,true_association,position_error_m,velocity_error_mps,track_id,"
           "position_3sigma_radius_m,position_contained,velocity_3sigma_radius_mps,velocity_contained\n";
    csv << std::setprecision(17);
    for (const auto &session : read_manifest(input))
    {
        if (session.split_ != "holdout")
            continue;
        const auto frames = tr::read_tracking_frames(session.frames_);
        const auto truth = read_truth(session.truth_);
        aligned(frames, truth);
        const auto tracker_cfg = prototype;
        tr::TargetTracker tracker(tracker_cfg);
        std::vector<tr::TrackingResult> results;
        std::optional<Vec3> previous_raw, previous_smooth;
        double previous_measurement_time = 0;
        std::optional<double> reversal_time;
        const double tau = -cfg.get("baseline_reference_dt_s") /
                            std::log(1 - cfg.get("baseline_reference_alpha"));
        for (std::size_t i = 0; i < frames.size(); ++i)
        {
            const double now = input_time(frames[i]);
            // 评分局部副本换到所选坐标；真值绝不进入tracker或基线的输入分支。
            auto scoring_truth = truth[i];
            scoring_truth.p_camera_ = truth_position(truth[i], tracker_cfg.coordinate_mode_);
            scoring_truth.v_camera_ = truth_velocity(truth[i], tracker_cfg.coordinate_mode_);
            ++frames_total;
            wrong_injected += truth[i].injected_error_;
            if (truth[i].reversed_)
            {
                ++reversals;
                reversal_time = truth[i].time_s_;
            }
            std::optional<Vec3> raw_p, raw_v, smooth_p, smooth_v;
            std::vector<std::string> adapter_events;
            const auto observations = tr::detail::adapt_measurements(frames[i], tracker_cfg, adapter_events);
            if (observations.size() == 1)
            {
                // world观测由输入的^W T_C变换而来，不取truth.p_world给基线或tracker。
                raw_p = observations.front().position_m_;
                if (previous_raw)
                {
                    const double dt = now - previous_measurement_time;
                    raw_v = (*raw_p - *previous_raw) / dt;
                    const double alpha = 1 - std::exp(-dt / tau);
                    smooth_p = *previous_smooth + alpha * (*raw_p - *previous_smooth);
                    smooth_v = (*smooth_p - *previous_smooth) / dt;
                    raw_metric.add(*raw_p, *raw_v, scoring_truth);
                    smooth_metric.add(*smooth_p, *smooth_v, scoring_truth);
                }
                else
                    smooth_p = raw_p;
                previous_raw = raw_p;
                previous_smooth = smooth_p;
                previous_measurement_time = now;
            }
            auto result = tracker.step(frames[i]);
            results.push_back(result);
            if (result.tracks_.empty())
            {
                csv << session.name_ << ',' << truth[i].frame_ << ",NO_TRACK,"
                    << truth[i].true_association_ << ",,,,,,,\n";
                continue;
            }
            const auto &track = result.tracks_.front();
            measured += track.state_ == tr::TrackState::Measured;
            coast += track.state_ == tr::TrackState::Coasting;
            lost += track.state_ == tr::TrackState::Lost;
            unbounded += !track.time_bounded_;
            qualified += track.qualified_for_g5_;
            if (truth[i].injected_error_ && track.state_ == tr::TrackState::Measured &&
                track.last_measurement_stamp_.frame_id_ == frames[i].stamp_.frame_id_)
            {
                ++wrong_accepted;
                wrong_confirmed += track.confirmed_;
            }
            csv << session.name_ << ',' << truth[i].frame_ << ',' << tr::track_state_name(track.state_)
                << ',' << truth[i].true_association_ << ',';
            std::optional<double> pe, ve, pr, vr;
            bool p_contained = false, v_contained = false;
            if (track.position_m_)
            {
                pe = cv::norm(*track.position_m_ - scoring_truth.p_camera_);
                csv << *pe;
            }
            csv << ',';
            if (track.velocity_mps_)
            {
                ve = cv::norm(*track.velocity_mps_ - scoring_truth.v_camera_);
                csv << *ve;
            }
            if (track.covariance_)
            {
                auto &counts = coverage[tr::track_state_name(track.state_)];
                if (pe)
                {
                    pr = three_sigma_radius(*track.covariance_, 0);
                    p_contained = *pe <= *pr;
                    ++counts[0];
                    counts[1] += p_contained;
                }
                if (ve)
                {
                    vr = three_sigma_radius(*track.covariance_, 3);
                    v_contained = *ve <= *vr;
                    ++counts[2];
                    counts[3] += v_contained;
                }
            }
            if (track.position_m_ && track.velocity_mps_)
            {
                tracker_metric.add(*track.position_m_, *track.velocity_mps_, scoring_truth);
                if (raw_v && smooth_v)
                {
                    paired_raw.add(*raw_p, *raw_v, scoring_truth);
                    paired_smooth.add(*smooth_p, *smooth_v, scoring_truth);
                    paired_tracker.add(*track.position_m_, *track.velocity_mps_, scoring_truth);
                }
                // 恢复指标用冻结profile尺度，仅作对照，不转成机械投放许可。
                if (reversal_time && *ve <= profile.velocity_std_limit_mps_)
                {
                    recovery.add(truth[i].time_s_ - *reversal_time);
                    ++recovered;
                    reversal_time.reset();
                }
            }
            csv << ',' << track.track_id_ << ',';
            if (pr) csv << *pr;
            csv << ',';
            if (pr) csv << p_contained;
            csv << ',';
            if (vr) csv << *vr;
            csv << ',';
            if (vr) csv << v_contained;
            csv << '\n';
        }
        tr::write_tracking_results(results, out / (session.name_ + "-tracks.yaml"));
    }
    if (!frames_total)
        throw std::runtime_error("lab holdout为空，不能判PASS");
    std::ofstream report(out / "verification.md");
    report << "# G4独立holdout对照\n\n未经过实车确认。输入绕过G2，是声明假设的3D测量流。"
           << "坐标域=" << profile.coordinate_mode_ << "，时间政策=" << profile.time_policy_
           << "。camera模式比较含ego的相机变化率；world模式在给定精确ego的假设下比较世界速度。"
              "一阶位置平滑是参考对照，不是旧二维稳定器逐值复刻。\n\n"
              "|方法|有速度样本数|位置RMSE(m)|速度RMSE(m/s)|最大速度误差(m/s)|\n|---|---:|---:|---:|---:|\n";
    report << std::setprecision(17);
    for (const auto &entry : std::vector<std::pair<std::string, const Metric *>>{
             {"raw有限差分", &raw_metric}, {"时间感知一阶平滑", &smooth_metric},
             {"TargetTracker全部可用输出", &tracker_metric}, {"共同样本raw", &paired_raw},
             {"共同样本平滑", &paired_smooth}, {"共同样本tracker", &paired_tracker}})
    {
        report << '|' << entry.first << '|' << entry.second->count_ << '|';
        if (entry.second->count_)
            report << std::sqrt(entry.second->p_sq_ / entry.second->count_) << '|'
                   << std::sqrt(entry.second->v_sq_ / entry.second->count_) << '|'
                   << entry.second->max_v_;
        else
            report << "不可计算|不可计算|不可计算";
        report << "|\n";
    }
    report << "\n总帧=" << frames_total << "，MEASURED=" << measured << "，COASTING=" << coast
           << "，LOST=" << lost << "，无速度/无track等其余样本亦保留CSV。\n\n错误测量注入="
           << wrong_injected << "，被当新测量接受=" << wrong_accepted
           << "（其中已确认track接受=" << wrong_confirmed << "；未确认seed也计入总接受数）"
           << "；未经时间资格输出=" << unbounded << "，G5资格输出=" << qualified << "。\n\n"
           << "世界目标实际反转=" << reversals << "，在profile条件速度尺度内恢复=" << recovered
           << "；未恢复/被后续反转覆盖的情况未强行填零。";
    if (recovery.samples_.size() >= 2)
        report << "已恢复延迟均值=" << recovery.mean() << "s，样本std=" << recovery.stddev() << "s。";
    report << "\n\n## 条件3sigma球经验包含率\n\n半径=3*sqrt(该P块最大特征值)，逐帧位置/速度真值误差范数与其比较。"
              "这是该假设域上的经验评分，不声称正态、固定99.7%覆盖或实车绝对界；超界不调参。\n\n"
              "|状态|位置包含/样本|位置比例|速度包含/样本|速度比例|\n|---|---:|---:|---:|---:|\n";
    for (const auto &[state, counts] : coverage)
    {
        report << '|' << state << '|' << counts[1] << '/' << counts[0] << '|';
        if (counts[0]) report << static_cast<double>(counts[1]) / counts[0];
        else report << "不可计算";
        report << '|' << counts[3] << '/' << counts[2] << '|';
        if (counts[2]) report << static_cast<double>(counts[3]) / counts[2];
        else report << "不可计算";
        report << "|\n";
    }
    report << "\n\n恢复判据只作条件对照，原始逐帧误差完整保留；不调profile追求全绿。"
              "高覆盖也不代表关联正确，须同时审错误接受数、共同样本误差及尾部。"
              "本工具无实车噪声/曝光/侧滑证据；不填生产阈值、不改G2、不输出控制。\n";
    if (!csv.good() || !report.good())
        throw std::runtime_error("lab 验证报告写失败");
    if (qualified != 0)
        throw std::runtime_error("lab 合成未验证输入错误获得G5资格");
    if (tracker_metric.count_ == 0 || paired_tracker.count_ == 0)
        throw std::runtime_error("lab 无速度输出或无共同对照样本；报告保留，但不能判实验通过");
}
} // namespace

// 三个命令共享配置与证据原件；生成/标定/独立验证职责分离，绝不在验证时调参数。
int main(int argc, char **argv)
{
    try
    {
        const auto args = parse_cli(argc, argv);
        const auto root = fs::path(__FILE__).parent_path().parent_path().parent_path();
        const auto config_path = args.count("--config") ? fs::path(args.at("--config")) :
                                                        root / "config/tracking_lab.yaml";
        const LabConfig config(config_path);
        const fs::path out(args.at("--out"));
        new_directory(out);
        fs::copy_file(config_path, out / "lab_config_input.yaml");
        const auto command = args.at("command");
        if (command == "generate")
            generate(config, out);
        else if (command == "calibrate")
            calibrate(config, args.at("--input"), out,
                      args.count("--profile") ? fs::path(args.at("--profile")) : out / "profile.yaml");
        else
            verify(config, args.at("--input"), out, args.at("--profile"));
        std::cout << "tracking_lab " << command << "完成，结果=" << out.string()
                  << "；仅合成/离线证据，未经过实车确认\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "tracking_lab错误: " << e.what() << '\n';
        return 1;
    }
}
