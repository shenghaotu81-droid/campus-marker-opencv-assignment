// 验位姿契约（T01-T25），理想数据下 estimate_planar 该 Unique 的 Unique、该 Ambiguous 的 Ambiguous
// g2_test::require 是断言，挂了会报哪个 T 没过
#include "support/g2_test_scene.hpp"
#include "pose/pose_geometry.hpp"

#include <algorithm>
#include <iostream>
#include <limits>

// 一套小型主动断言覆盖物理对应、预算、多解、时间和几何异常，不引入新测试框架。
int main()
{
    try
    {
        using namespace mark::pose;
        const auto c = g2_test::config();
        const auto stamp_ = g2_test::stamp(c);
        const auto points = g2_test::pixels(c);
        PoseEstimator solver(c);
        const auto base = solver.estimate_planar(c.mark_points_m_, points, c.camera_.image_size_,
                                                 stamp_, c.mark_target_id_);
        g2_test::require(base.status_ == PoseStatus::Unique && base.T_camera_target_,
                          "T01 理想倾斜四点必须有质量可区分的唯一解");
        g2_test::require(cv::norm(base.T_camera_target_->t_m_ - g2_test::truth().t_m_) < 1e-7 &&
            detail::rotation_distance(base.T_camera_target_->rvec_, g2_test::truth().rvec_) < 1e-6,
            "T02 object->camera 单位与旋转必须恢复真值");
        g2_test::require(!base.control_usable_ && !base.covariance_se3_,
                          "T03 离线结果不能宣称硬件可用或零协方差");
        auto raw = g2_test::raw(c);
        std::array<int, 4> perm{{0, 1, 2, 3}};
        do
        {
            auto shuffled = raw;
            shuffled.detections[0].attributes.orientation = perm;
            for (std::size_t p = 0; p < 4; ++p)
                shuffled.detections[0].corners[perm[p]] = points[p];
            // 放入明显不同的稳定结果；证明 PnP 没有误读 tracks。
            shuffled.tracks.push_back({0, mark::Detection{}});
            const auto result = solver.estimate_mark(shuffled, 0, c.camera_.image_size_, stamp_);
            g2_test::require(result.status_ == PoseStatus::Unique &&
                cv::norm(result.T_camera_target_->t_m_ - g2_test::truth().t_m_) < 1e-5,
                "T04 24种屏幕槽置换必须还原同一物理位姿");
        } while (std::next_permutation(perm.begin(), perm.end()));
        raw.detections[0].attributes.orientation.reset();
        g2_test::require(solver.estimate_mark(raw, 0, c.camera_.image_size_, stamp_).status_ ==
                          PoseStatus::CorrespondenceMissing, "T05 缺方向不能猜对应");
        raw.detections[0].attributes.orientation = std::array<int, 4>{{0, 0, 2, 3}};
        g2_test::require(!solver.estimate_mark(raw, 0, c.camera_.image_size_, stamp_).T_camera_target_,
                          "T06 非排列方向必须拒绝");
        auto wrong_stamp = stamp_;
        ++wrong_stamp.frame_id_;
        g2_test::require(solver.estimate_mark(raw, 0, c.camera_.image_size_, wrong_stamp).status_ ==
                          PoseStatus::InvalidInput, "T07 帧绑定错误必须拒绝");
        wrong_stamp = stamp_;
        wrong_stamp.camera_id_ = "other-camera";
        g2_test::require(solver.estimate_planar(c.mark_points_m_, points, c.camera_.image_size_,
            wrong_stamp, c.mark_target_id_).status_ == PoseStatus::InvalidInput,
            "T08 相机身份不符必须拒绝");
        g2_test::require(solver.estimate_planar(c.mark_points_m_, points, {720, 540}, stamp_,
            c.mark_target_id_).status_ == PoseStatus::InvalidInput, "T09 不暗缩放 K");
        wrong_stamp = stamp_;
        wrong_stamp.pixel_domain_ = PixelDomain::Unknown;
        g2_test::require(solver.estimate_planar(c.mark_points_m_, points, c.camera_.image_size_,
            wrong_stamp, c.mark_target_id_).status_ == PoseStatus::InvalidInput,
            "T10 未知/去畸变像素域不能配原 K/dist");
        auto bad = points;
        bad[0].x = std::numeric_limits<double>::quiet_NaN();
        g2_test::require(!solver.estimate_planar(c.mark_points_m_, bad, c.camera_.image_size_,
            stamp_, c.mark_target_id_).T_camera_target_, "T11 NaN角点必须拒绝");
        bad = points;
        bad[2] = bad[1];
        g2_test::require(!solver.estimate_planar(c.mark_points_m_, bad, c.camera_.image_size_,
            stamp_, c.mark_target_id_).T_camera_target_, "T12 重复/退化角点必须拒绝");
        bad = points;
        bad[0].x = -1;
        g2_test::require(!solver.estimate_planar(c.mark_points_m_, bad, c.camera_.image_size_,
            stamp_, c.mark_target_id_).T_camera_target_, "T13 画外角点必须拒绝");
        auto no_budget = c;
        no_budget.budgets_.max_rmse_px_.reset();
        auto result = PoseEstimator(no_budget).estimate_planar(c.mark_points_m_, points,
            c.camera_.image_size_, stamp_, c.mark_target_id_);
        g2_test::require(result.status_ == PoseStatus::BudgetMissing && !result.candidates_.empty() &&
                          !result.T_camera_target_, "T14 未标定预算只给候选");
        auto permissive = c;
        // 故意构造双解都合格的测试条件；不是为视频放宽生产阈值。
        permissive.budgets_.max_rmse_px_ = 1000;
        permissive.budgets_.max_corner_error_px_ = 1000;
        result = PoseEstimator(permissive).estimate_planar(c.mark_points_m_, points,
            c.camera_.image_size_, stamp_, c.mark_target_id_);
        g2_test::require(result.status_ == PoseStatus::Ambiguous && result.candidates_.size() == 2 &&
                          !result.selected_index_ && !result.T_camera_target_,
                          "T15 双合格解必须歧义，不能选择较小重投影误差");
        auto large = c.mark_points_m_;
        for (auto &p : large)
            p *= 1000;
        result = solver.estimate_planar(large, points, c.camera_.image_size_, stamp_, "mm-as-m-fixture");
        g2_test::require(result.T_camera_target_ &&
            cv::norm(result.T_camera_target_->t_m_ - g2_test::truth().t_m_ * 1000) < 1e-3,
            "T16 单目尺度错误不可观测，必须由模型单位保证而非残差检出");
        auto negative = g2_test::truth();
        negative.t_m_[2] = -1;
        g2_test::require(!detail::evaluate_pose(negative, c.mark_points_m_, points,
            c.camera_.K_, c.camera_.dist_).reasons_.empty(), "T17 所有物理点必须正深度");
        auto lm = c;
        lm.selection_.refine_lm_ = true;
        result = PoseEstimator(lm).estimate_planar(c.mark_points_m_, points, c.camera_.image_size_,
            stamp_, c.mark_target_id_);
        g2_test::require(result.status_ == PoseStatus::Unique && result.refined_rmse_px_ &&
            cv::norm(result.T_camera_target_->t_m_ - g2_test::truth().t_m_) < 1e-6,
            "T18 LM只能改善已选解，不能替代消歧");
        raw = g2_test::raw(c);
        raw.status = mark::Status::NOT_DETECTED;
        g2_test::require(solver.estimate_mark(raw, 0, c.camera_.image_size_, stamp_).status_ ==
                          PoseStatus::NotDetected, "T19 当前无检测不得桥接旧位姿");
        auto prior_cfg = permissive;
        prior_cfg.selection_.prior_enabled_ = true;
        prior_cfg.selection_.prior_max_age_s_ = 0.1;
        prior_cfg.selection_.prior_max_translation_m_ = 0.001;
        prior_cfg.selection_.prior_max_rotation_rad_ = 0.001;
        PosePrior prior{stamp_, c.mark_target_id_, g2_test::truth(), true};
        --prior.stamp_.frame_id_;
        *prior.stamp_.exposure_us_ -= 1000;
        prior.stamp_.received_us_ -= 1000;
        result = PoseEstimator(prior_cfg).estimate_planar(c.mark_points_m_, points,
            c.camera_.image_size_, stamp_, c.mark_target_id_, prior);
        g2_test::require(result.status_ == PoseStatus::Unique,
                          "T20 有独立可信历史时可以排除不符候选");
        prior.stamp_.session_id_ = "old-session";
        result = PoseEstimator(prior_cfg).estimate_planar(c.mark_points_m_, points,
            c.camera_.image_size_, stamp_, c.mark_target_id_, prior);
        g2_test::require(result.status_ == PoseStatus::Ambiguous,
                          "T21 跨会话历史不能消歧");
        auto invalid_config = c;
        invalid_config.budgets_.max_rmse_px_ = -1;
        bool threw = false;
        try { PoseEstimator rejected(invalid_config); }
        catch (const std::runtime_error &) { threw = true; }
        g2_test::require(threw, "T22 负数预算必须拒绝，不能取绝对值或降级默认值");
        g2_test::require(cv::norm(c.mark_points_m_[0] - cv::Point3d(0, 0, 0)) < 1e-12 &&
            cv::norm(c.mark_points_m_[1] - cv::Point3d(0.08, 0, 0)) < 1e-12 &&
            cv::norm(c.mark_points_m_[2] - cv::Point3d(0.08, 0.08, 0)) < 1e-12,
            "T23 图纸毫米转米且 P1 必须绑定 M1.v1");
        auto zero_dist = c;
        zero_dist.camera_.dist_ = cv::Mat::zeros(1, 5, CV_64F);
        const auto zero_pixels = g2_test::pixels(zero_dist);
        result = PoseEstimator(zero_dist).estimate_planar(zero_dist.mark_points_m_, zero_pixels,
            zero_dist.camera_.image_size_, stamp_, zero_dist.mark_target_id_);
        g2_test::require(result.T_camera_target_ &&
            cv::norm(result.T_camera_target_->t_m_ - g2_test::truth().t_m_) < 1e-7,
            "T24 零畸变与非零畸变均能正确投影/还原");
        prior.stamp_.session_id_ = stamp_.session_id_;
        prior.stamp_.exposure_us_ = 1;
        result = PoseEstimator(prior_cfg).estimate_planar(c.mark_points_m_, points,
            c.camera_.image_size_, stamp_, c.mark_target_id_, prior);
        g2_test::require(result.status_ == PoseStatus::Ambiguous,
                          "T25 过期历史不得消歧");
        std::cout << "G2 pose contract T01-T25 PASS (24 permutations included)\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}