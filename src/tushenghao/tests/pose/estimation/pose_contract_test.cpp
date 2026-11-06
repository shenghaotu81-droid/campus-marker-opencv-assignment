#include "g2_test_scene.hpp"
#include "vision/pose/pose_io.hpp"
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
        require(bool(p.estimate_), "nominal estimate missing");
        require(p.estimate_->rmse_px_ < 1e-6 && cv::norm(p.estimate_->pose_.t_m_ - g2_test::truth().t_m_) < 1e-6, "ideal metric pose");
        const auto spread = p.estimate_->translation_spread_m2_;
        require(cv::checkRange(cv::Mat(spread)) && cv::norm(cv::Mat(spread - spread.t())) == 0,
                "translation spread is not finite/symmetric");
        mark::FrameResult absent; absent.frame_id = stamp.frame_id_; absent.timestamp_us = stamp.received_us_;
        absent.status = mark::Status::NOT_DETECTED;
        require(!solver.estimate_mark(absent, 0, cfg.camera_.image_size_, stamp).estimate_,
                "missing target conflated with estimate or solver failure");
        auto noisy = xy;
        noisy[0].x += 15;
        auto q = solver.estimate_planar(cfg.mark_points_m_, noisy, cfg.camera_.image_size_, stamp, cfg.mark_target_id_);
        require(q.estimate_ && q.estimate_->rmse_px_ > 1.1, "old quality gate still rejects finite pose");
        require(cv::norm(cv::Mat(q.estimate_->translation_spread_m2_)) > 0, "two-IPPE information lost at G2 boundary");
        auto bad = xy;
        bad[0].x = std::numeric_limits<double>::quiet_NaN();
        require(!solver.estimate_planar(cfg.mark_points_m_, bad, cfg.camera_.image_size_, stamp, cfg.mark_target_id_).estimate_, "NaN accepted");
        auto behind = g2_test::truth();
        behind.t_m_[2] = -1;
        require(!detail::evaluate_pose(behind, cfg.mark_points_m_, xy, cfg.camera_.K_, cfg.camera_.dist_), "negative depth");
        cv::FileStorage output("pose.yaml", cv::FileStorage::WRITE | cv::FileStorage::MEMORY);
        output << "observations" << "[";
        write_pose_observation(output, p);
        output << "]";
        cv::FileStorage read(output.releaseAndGetString(), cv::FileStorage::READ | cv::FileStorage::MEMORY);
        const auto node = read["observations"][0];
        require(node["candidates"].empty() && node["selected_index"].empty() && node["rvec"].empty(),
                "solver internals or duplicated nominal pose serialized");
        require(!node["estimate"]["translation_spread_m2"].empty() && node["status"].string() == "ESTIMATED",
                "current nominal estimate contract not serialized");
        // 原调用方改Mat不得污染求解器，不需专门为测试提供config快照API。
        cfg.camera_.K_.setTo(0);
        require(bool(solver.estimate_planar(cfg.mark_points_m_, xy, cfg.camera_.image_size_, stamp, cfg.mark_target_id_).estimate_), "aliased K");
        return 0;
    }
    catch (const std::exception &e){
        std::cerr << e.what() << '\n';
        return 1;
    }
}
