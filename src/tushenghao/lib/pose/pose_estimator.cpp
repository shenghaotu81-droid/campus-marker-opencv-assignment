// MARK适配/IPPE/门控/LM（Pnp核心）
#include "mark/pose_estimator.hpp"
#include "pose/pose_geometry.hpp"

#include <cmath>
#include <set>
#include <opencv2/calib3d.hpp>

namespace mark::pose
{
namespace
{
// 筛候选（安装约束：z 范围/法向）
// 不同安装条件分别排除候选；没有配置约束时不擅自假设目标法向朝向。
bool passes_constraints(const RigidPose &p, const SelectionConfig &s)
{
    cv::Matx33d r;
    cv::Rodrigues(p.rvec_, r);  // rvec ↔ 旋转矩阵互转，passes_constraints 里用它取 R(2,2) 做法向量检查
    return (!s.min_z_m_ || (p.t_m_[2] >= *s.min_z_m_ && p.t_m_[2] <= *s.max_z_m_)) &&
           (!s.min_normal_z_ || (r(2, 2) >= *s.min_normal_z_ && r(2, 2) <= *s.max_normal_z_));
}

// 判断 prior 能不能用
// 历史只有在身份、时钟、严格先后与年龄都匹配时可参与消歧；没有默认追踪器。
bool prior_is_usable(const PosePrior &p, const CaptureStamp &s,
                     const std::string &id_, const SelectionConfig &c)
{
    if (!c.prior_enabled_ || !p.trusted_unique_ || !detail::valid_stamp(p.stamp_) ||
        !s.exposure_us_ || !p.stamp_.exposure_us_ || !s.time_uncertainty_s_ ||
        !p.stamp_.time_uncertainty_s_ || p.target_id_ != id_ ||
        p.stamp_.session_id_ != s.session_id_ || p.stamp_.camera_id_ != s.camera_id_ ||
        p.stamp_.clock_domain_ != s.clock_domain_ || p.stamp_.frame_id_ >= s.frame_id_ ||
        *p.stamp_.exposure_us_ >= *s.exposure_us_)
        return false;
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(p.pose_.rvec_[i]) || !std::isfinite(p.pose_.t_m_[i]))
            return false;
    // 时间误差加入最坏年龄，不能把不确定性越大理解成历史越新。
    const long double age = (static_cast<long double>(*s.exposure_us_) -
                             *p.stamp_.exposure_us_) / 1e6L;
    return age + *s.time_uncertainty_s_ + *p.stamp_.time_uncertainty_s_ <= *c.prior_max_age_s_;
}

// 候选离 prior 够不够近
bool passes_prior(const RigidPose &p, const PosePrior &prior, const SelectionConfig &s)
{
    return cv::norm(p.t_m_ - prior.pose_.t_m_) <= *s.prior_max_translation_m_ &&
           detail::rotation_distance(p.rvec_, prior.pose_.rvec_) <= *s.prior_max_rotation_rad_;     // norm算向量长度，passes_prior 里算平移差
}
} // namespace

// 构造函数：收 G2Config，先 validate_pose_config 校验，再把 K/dist clone() 深拷贝。
PoseEstimator::PoseEstimator(G2Config config) : config_(std::move(config))
{
    validate_pose_config(config_);
    // cv::Mat 浅拷贝会允许外部改 K/dist；持有私有副本，避免求解中途被修改。
    config_.camera_.K_ = config_.camera_.K_.clone();
    config_.camera_.dist_ = config_.camera_.dist_.clone();
}

// MARK完整处理：验输入、转物理序、调求解、定 control_usable_
// estimate_mark 做 MARK 特有的预处理（查时间戳、转物理序），然后把 4 对 2D-3D 点丢给 estimate_planar 做真正的 IPPE 求解
/*   MARK 特有处理流程：
    1. 验证输入数据的有效性（验检测器输出：帧号对上、状态是 DETECTED、序号不越界）
    2. 转物理序：pixels[p] = corners[orientation[p]]，屏幕序→物理序
    3. 调用 IPPE 算法求解位姿（planner常规）
    4. 根据求解结果决定控制是否可用（定 control_usable_：Unique + 4 个硬件验证 + 时间误差，缺一不可）
*/
/*  estimate_planar 给核心：r/t（T_camera_target_）+ 状态（Unique/Ambiguous…）。
estimate_mark 拿到后补 MARK 特有的：source_="MARK"、detection_index_，再按 MARK 的 4 个硬件验证算 control_usable_。
*/
// orientation[p] 是物理角到屏幕 slot 的映射；不能把屏幕排序当物理身份。
PoseObservation PoseEstimator::estimate_mark(
    const FrameResult &raw, std::size_t index, cv::Size size, const CaptureStamp &stamp_,
    const std::optional<PosePrior> &prior) const
{
    PoseObservation out;
    out.stamp_ = stamp_;
    out.source_ = "MARK";
    out.target_id_ = config_.mark_target_id_;
    out.detection_index_ = index;
    const auto frame_time = stamp_.exposure_us_.value_or(stamp_.received_us_);
    // 门控链：帧时间戳对不上 → 返回；orientation 缺失 → CorrespondenceMissing；预算没标定 → BudgetMissing（只给候选）；合格候选不是恰好 1 个 → NoSolution/Ambiguous；恰好 1 个 → Unique
    if (raw.frame_id != stamp_.frame_id_ || raw.timestamp_us != frame_time)
    {
        out.reasons_.push_back("FRAME_STAMP_MISMATCH");
        return out;
    }
    if (raw.status != Status::DETECTED)
    {
        out.status_ = raw.status == Status::NOT_DETECTED ? PoseStatus::NotDetected : PoseStatus::InvalidInput;
        out.reasons_.push_back("RAW_DETECTOR_STATUS_UNAVAILABLE");
        return out;
    }
    if (index >= raw.detections.size())
    {
        out.reasons_.push_back("RAW_DETECTION_INDEX_OUT_OF_RANGE");
        return out;
    }
    const auto &d = raw.detections[index];
    if (!d.attributes.orientation)
    {
        out.status_ = PoseStatus::CorrespondenceMissing;
        out.reasons_.push_back("PHYSICAL_ORIENTATION_MISSING");
        return out;
    }
    std::set<int> seen;
    std::array<cv::Point2d, 4> pixels;
    for (std::size_t p = 0; p < pixels.size(); ++p)
    {
        const int slot = (*d.attributes.orientation)[p];
        if (slot < 0 || slot >= 4 || !seen.insert(slot).second)
        {
            out.reasons_.push_back("ORIENTATION_NOT_PERMUTATION");
            return out;
        }
        pixels[p] = d.corners[slot];     // orientation 映射：屏幕序转物理序
    }
    out = estimate_planar(config_.mark_points_m_, pixels, size, stamp_,
                          config_.mark_target_id_, prior);
    out.source_ = "MARK";
    out.detection_index_ = index;
    // control_usable_:Unique + 4 个 hardware_verified + 时间误差合格，缺一不可
    out.control_usable_ = out.status_ == PoseStatus::Unique &&
        config_.camera_.hardware_verified_ && config_.mark_geometry_verified_ &&
        config_.budgets_.hardware_verified_ && config_.selection_.constraints_verified_ &&
        config_.budgets_.max_time_uncertainty_s_ && stamp_.exposure_us_ &&
        stamp_.time_uncertainty_s_ &&
        *stamp_.time_uncertainty_s_ <= *config_.budgets_.max_time_uncertainty_s_;
    if (!out.control_usable_)
        out.reasons_.push_back("MARK_MEASUREMENT_NOT_HARDWARE_QUALIFIED");
    return out;
}

// 通用求解：给 4 对 2D-3D 点算位姿（目标系到相机系（K/dist）的变换（R/t））
/*  estimate_planar 流程：
1. 验输入：时间戳、相机 ID、图像尺寸、四边形合法，模型点 z=0 且凸
2. 查面积：四边形太小直接毙（QUAD_BELOW_APPROVED_AREA）
3. IPPE 求解：solvePnPGeneric(..., SOLVEPNP_IPPE) 拿多候选
4. 逐候选筛（四道）：
evaluate_pose 独立复核（重投影 RMSE、正深度）
预算：RMSE/最大误差超了毙
passes_constraints：安装约束（z 范围、法向）
passes_prior：离 prior 太远毙（开了才查）
5. 预算没标定：直接 BudgetMissing返回，候选留着审计
6. 数合格（eligible_）：0 个 → NoSolution；1 个 → Unique（可LM精修：refine_lm_ 开了，调 solvePnPRefineLM 试精修）；2+ → Ambiguous
*/
// 保留 IPPE 的平面多解；仅一个候选满足独立约束才发布，不按最小残差强选。
PoseObservation PoseEstimator::estimate_planar(
    const std::array<cv::Point3d, 4> &object,
    const std::array<cv::Point2d, 4> &image,
    cv::Size size, const CaptureStamp &stamp_, const std::string &id_,
    const std::optional<PosePrior> &prior) const
{
    PoseObservation out;
    out.stamp_ = stamp_;
    out.source_ = "PLANAR";
    out.target_id_ = id_;
    if (!detail::valid_stamp(stamp_) || id_.empty() ||
        stamp_.camera_id_ != config_.camera_.camera_id_ || size != config_.camera_.image_size_ ||
        !detail::valid_quad(image, size))
    {
        out.reasons_.push_back("STAMP_CAMERA_SIZE_OR_QUAD_INVALID");
        return out;
    }
    // 通用入口只接受本期四点平面；防止 NaN、共线、乱序模型进入 IPPE。
    std::array<cv::Point2d, 4> model_xy;
    for (std::size_t i = 0; i < object.size(); ++i)
    {
        if (!std::isfinite(object[i].x) || !std::isfinite(object[i].y) || object[i].z != 0)
        {
            out.reasons_.push_back("OBJECT_NOT_FINITE_Z0_PLANE");
            return out;
        }
        model_xy[i] = {object[i].x, object[i].y};
    }
    double previous_cross = 0;
    for (std::size_t i = 0; i < model_xy.size(); ++i)
    {
        const double cross = (model_xy[(i + 1) % 4] - model_xy[i]).cross(
            model_xy[(i + 2) % 4] - model_xy[(i + 1) % 4]);
        if (cross == 0 || (i && cross * previous_cross <= 0))
        {
            out.reasons_.push_back("OBJECT_QUAD_DEGENERATE");
            return out;
        }
        previous_cross = cross;
    }
    const auto &b = config_.budgets_;
    const bool budgets_ready = b.max_rmse_px_ && b.max_corner_error_px_ && b.min_quad_area_px2_;
    if (b.min_quad_area_px2_ && detail::quad_area(image) < *b.min_quad_area_px2_)
    {
        out.status_ = PoseStatus::NoSolution;
        out.reasons_.push_back("QUAD_BELOW_APPROVED_AREA");
        return out;
    }
    const bool use_prior = prior && prior_is_usable(*prior, stamp_, id_, config_.selection_);
    if (prior && !use_prior)
        out.reasons_.push_back("PRIOR_IGNORED");
    const std::vector<cv::Point3d> objects(object.begin(), object.end());
    const std::vector<cv::Point2d> pixels(image.begin(), image.end());
    std::vector<cv::Mat> rotations, translations;
    try
    {
        // 理解：
        // r（rvec）：旋转向量，3 个数。方向=旋转轴，长度=转多少弧度。Rodrigues 转成 3×3 矩阵用（MARK 朝哪：MARK 自身坐标系相对相机转了多少；R=单位阵时，MARK 正对相机（和像平面平行）；有旋转就是斜了。）。
        // t（tvec）：平移向量，3 个数 (x,y,z)，单位米。目标原点在相机坐标系里的位置（MARK 在哪：z 是距离（米），x/y 是左右/上下偏了多少。）。
        cv::solvePnPGeneric(objects, pixels, config_.camera_.K_, config_.camera_.dist_,
                            rotations, translations, false, cv::SOLVEPNP_IPPE);      // 解 PnP 问题——已知 3D 点和它们在图上的 2D 投影，反推相机位姿（R/t）。IPPE 是专给平面目标的算法，会返回多个候选（平面有二义性），不帮你选。
        // IPPE 对平面目标一般返回 2 个候选
        // prior可以用来排歧义Ambiguous，如果有个独立可信的历史位姿（prior），就拿 rotation_distance 和平移距离比：谁离 prior 近留谁，远的毙掉。现在默认关闭，G4 的 Kalman 预测可以当 prior 用
        // 筛候选：evaluate_pose 独立复核（重投影误差、正深度）→ 查预算（RMSE/最大误差超了就毙）→ passes_constraints安装约束（z 范围/法向） → passes_prior（prior 开了才查）；四道全过才 eligible_=true；挂掉的 eligible_=false，最后数还剩几个合格的
        for (std::size_t i = 0; i < rotations.size(); ++i)
        {
            RigidPose p;
            for (int k = 0; k < 3; ++k)
            {
                p.rvec_[k] = rotations[i].at<double>(k);
                p.t_m_[k] = translations[i].at<double>(k);
            }
            auto candidate = detail::evaluate_pose(p, object, image,
                                                    config_.camera_.K_, config_.camera_.dist_);
            if (candidate.reasons_.empty() && budgets_ready)
            {
                if (candidate.rmse_px_ > *b.max_rmse_px_ ||
                    candidate.max_error_px_ > *b.max_corner_error_px_)
                    candidate.reasons_.push_back("REPROJECTION_BUDGET_EXCEEDED");
                if (!passes_constraints(p, config_.selection_))
                    candidate.reasons_.push_back("INSTALLATION_CONSTRAINT_REJECTED");
                if (use_prior && !passes_prior(p, *prior, config_.selection_))
                    candidate.reasons_.push_back("TRUSTED_PRIOR_REJECTED");
                candidate.eligible_ = candidate.reasons_.empty();
            }
            out.candidates_.push_back(std::move(candidate));
        }
        if (out.candidates_.empty())
        {
            out.status_ = PoseStatus::NoSolution;
            out.reasons_.push_back("IPPE_RETURNED_NO_CANDIDATE");
            return out;
        }
        if (!budgets_ready)
        {
            out.status_ = PoseStatus::BudgetMissing;
            out.reasons_.push_back("UNMEASURED_POSE_BUDGETS");
            return out;
        }
        // eligible_ 计数，0 个 → NoSolution，1 个 → Unique，2+ → Ambiguous
        std::vector<std::size_t> eligible_;
        for (std::size_t i = 0; i < out.candidates_.size(); ++i)
            if (out.candidates_[i].eligible_)
                eligible_.push_back(i);
        if (eligible_.size() != 1)
        {
            out.status_ = eligible_.empty() ? PoseStatus::NoSolution : PoseStatus::Ambiguous;
            out.reasons_.push_back(eligible_.empty() ? "ALL_CANDIDATES_REJECTED" : "PLANAR_AMBIGUITY");
            return out;
        }
        const auto index = eligible_.front();
        const auto &selected = out.candidates_[index];
        out.selected_index_ = index;
        out.T_camera_target_ = selected.pose_;
        out.selected_rmse_px_ = selected.rmse_px_;
        if (config_.selection_.refine_lm_)
        {
            try
            {
                auto refined = selected.pose_;
                cv::solvePnPRefineLM(objects, pixels, config_.camera_.K_, config_.camera_.dist_,
                                     refined.rvec_, refined.t_m_);    // Levenberg-Marquardt：非线性最小二乘优化。solvePnPRefineLM 拿已选中的解当起点，迭代微调 R/t 让重投影误差更小。只是"打磨"，选解还是前面的门控说了算。
                const auto measured = detail::evaluate_pose(refined, object, image,
                                                             config_.camera_.K_, config_.camera_.dist_);
                if (measured.metrics_valid_)
                    out.refined_rmse_px_ = measured.rmse_px_;
                // LM 只能改进已选解，不能越过质量/安装/历史边界，原始候选证据保持不变。
                if (measured.reasons_.empty() && measured.rmse_px_ <= selected.rmse_px_ &&
                    measured.rmse_px_ <= *b.max_rmse_px_ && measured.max_error_px_ <= *b.max_corner_error_px_ &&
                    passes_constraints(refined, config_.selection_) &&
                    (!use_prior || passes_prior(refined, *prior, config_.selection_)))
                    out.T_camera_target_ = refined;
                else
                    out.reasons_.push_back("LM_REJECTED_KEEP_ORIGINAL");
            }
            catch (const cv::Exception &)
            {
                // 可选精修失败不能抹掉已通过检查的原始解；保留原解并记录失败。
                out.reasons_.push_back("LM_EXCEPTION_KEEP_ORIGINAL");
            }
        }
        out.status_ = PoseStatus::Unique;
        // 通用入口不知道物理模型是否已验证；控制级资格只能由 MARK/Tag 类型适配器赋予。
        out.control_usable_ = false;
    }
    catch (const cv::Exception &)
    {
        out.status_ = PoseStatus::NoSolution;
        out.selected_index_.reset();
        out.T_camera_target_.reset();
        out.control_usable_ = false;
        out.reasons_.push_back("OPENCV_SOLVER_EXCEPTION");
    }
    return out;
}
} // namespace mark::pose