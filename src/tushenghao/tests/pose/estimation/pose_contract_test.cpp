#include "g2_test_scene.hpp"
#include "validation/pose_geometry.hpp"
#include <iostream>
#include <limits>
int main()
{
    try{
        using namespace mark::pose;
        using g2_test::require;
        auto cfg = g2_test::config();
        auto stamp = g2_test::stamp(cfg);
        auto xy = g2_test::pixels(cfg);
        PoseEstimator solver(cfg);
        auto p = solver.estimate_planar(cfg.mark_points_m_, xy, cfg.camera_.image_size_, stamp, cfg.mark_target_id_);
        require(p.measurement() && p.candidates_.size() == 2 && p.status_ == PoseStatus::Ambiguous, "IPPE physical alternatives lost");
        require(p.measurement()->rmse_px_ < 1e-6 && cv::norm(p.measurement()->pose_.t_m_ - g2_test::truth().t_m_) < 1e-6, "ideal metric pose");
        auto noisy = xy;
        noisy[0].x += 15;
        auto q = solver.estimate_planar(cfg.mark_points_m_, noisy, cfg.camera_.image_size_, stamp, cfg.mark_target_id_);
        require(q.measurement() && q.measurement()->rmse_px_ > 1.1, "old quality gate still rejects finite pose");
        auto bad = xy;
        bad[0].x = std::numeric_limits<double>::quiet_NaN();
        require(!solver.estimate_planar(cfg.mark_points_m_, bad, cfg.camera_.image_size_, stamp, cfg.mark_target_id_).measurement(), "NaN accepted");
        auto behind = g2_test::truth();
        behind.t_m_[2] = -1;
        require(!detail::evaluate_pose(behind, cfg.mark_points_m_, xy, cfg.camera_.K_, cfg.camera_.dist_), "negative depth");
        // 原调用方改Mat不得污染求解器，不需专门为测试提供config快照API。
        cfg.camera_.K_.setTo(0);
        require(solver.estimate_planar(cfg.mark_points_m_, xy, cfg.camera_.image_size_, stamp, cfg.mark_target_id_).measurement(), "aliased K");
        return 0;
    }
    catch (const std::exception &e){
        std::cerr << e.what() << '\n';
        return 1;
    }
}
