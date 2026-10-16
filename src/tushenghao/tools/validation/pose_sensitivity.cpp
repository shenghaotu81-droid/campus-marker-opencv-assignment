// 只测量，不填预算
// g2 Step 7 的敏感性工具
// 批量生成合成数据（3 距离×3 倾角×12 旋转×4 噪声×8 重复=3456 组），跑位姿估计，把误差写进 sensitivity.csv 和 candidate_errors.csv
// 它只产数据，阈值后面用 Python 算 3σ 再手填进 pose.yaml
#include "mark/pose_estimator.hpp"
#include "mark/pose_estimator.hpp"
#include "../../lib/pose/pose_geometry.hpp"  // 加这一行，为了 detail::quad_area

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <opencv2/calib3d.hpp>

// 只写合成敏感性证据，不填生产预算；真值只进入评分，不进入候选选择。
int main(int argc, char **argv)
{
    try
    {
        if (argc != 2)
            throw std::runtime_error("用法: pose_sensitivity <不存在的输出目录>");
        const std::filesystem::path out(argv[1]);
        if (std::filesystem::exists(out))
            throw std::runtime_error("拒绝覆盖已有敏感性结果");
        const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
        const auto c = mark::pose::load_pose_config(root / "config/pose.yaml");
        mark::pose::PoseEstimator solver(c);
        std::filesystem::create_directories(out);
        std::ofstream csv(out / "sensitivity.csv");
        if (!csv)
            throw std::runtime_error("CSV 打不开");
        csv << "distance_m,tilt_rad,roll_deg,noise_sigma_px,repeat,status,candidates,"
               "truth_nearest_candidate_translation_error_m,selected_translation_error_m\n";
        csv << std::setprecision(17);
        std::ofstream candidates(out / "candidate_errors.csv");
        if (!candidates)
            throw std::runtime_error("候选误差 CSV 打不开");
        candidates << "sample_id,candidate_index,rmse_px,min_depth_m,translation_error_m,rotation_error_rad,max_error_px,quad_area_px2\n";
        candidates << std::setprecision(17);
        std::mt19937 rng(20261015);
        std::normal_distribution<double> normal(0, 1);
        std::uint64_t sequence = 0, unique = 0, invalid = 0;
        // 这些距离/倾角/噪声仅是离线探针假设，不宣称比赛实际分布或公制精度预算。
        for (double distance : {0.35, 0.7, 1.4})
            for (double tilt : {0.1, 0.35, 0.6})
                for (int roll = 0; roll < 360; roll += 30)
                    for (double sigma : {0.0, 0.25, 0.5, 1.0})
                        for (int repeat = 0; repeat < 8; ++repeat)
                        {
                            cv::Matx33d rx, ry, rz;
                            cv::Rodrigues(cv::Vec3d(tilt, 0, 0), rx);
                            cv::Rodrigues(cv::Vec3d(0, -tilt / 2, 0), ry);
                            cv::Rodrigues(cv::Vec3d(0, 0, roll * CV_PI / 180), rz);
                            const auto rotation = rz * ry * rx;
                            cv::Vec3d rvec_;
                            cv::Rodrigues(rotation, rvec_);
                            const auto center = (c.mark_points_m_[0] + c.mark_points_m_[2]) * 0.5;
                            const cv::Vec3d translation = cv::Vec3d(0, 0, distance) -
                                rotation * cv::Vec3d(center.x, center.y, center.z);
                            std::vector<cv::Point2d> pixels;
                            cv::projectPoints(std::vector<cv::Point3d>(c.mark_points_m_.begin(),
                                c.mark_points_m_.end()), rvec_, translation,
                                c.camera_.K_, c.camera_.dist_, pixels);
                            std::array<cv::Point2d, 4> observed;
                            for (std::size_t p = 0; p < observed.size(); ++p)
                                observed[p] = pixels[p] + cv::Point2d(normal(rng) * sigma,
                                                                     normal(rng) * sigma);
                            mark::pose::CaptureStamp stamp_;
                            stamp_.session_id_ = "sensitivity-seed-20261015";
                            stamp_.camera_id_ = c.camera_.camera_id_;
                            stamp_.clock_domain_ = "synthetic-clock";
                            stamp_.pixel_domain_ = mark::pose::PixelDomain::RawDistorted;
                            stamp_.frame_id_ = sequence++;
                            const auto result = solver.estimate_planar(c.mark_points_m_, observed,
                                c.camera_.image_size_, stamp_, c.mark_target_id_);
                            unique += result.status_ == mark::pose::PoseStatus::Unique;
                            invalid += result.status_ == mark::pose::PoseStatus::InvalidInput;
                            csv << distance << ',' << tilt << ',' << roll << ',' << sigma << ','
                                << repeat << ',' << mark::pose::pose_status_name(result.status_)
                                << ',' << result.candidates_.size() << ',';
                            if (!result.candidates_.empty())
                            {
                                double nearest = INFINITY;
                                for (const auto &candidate : result.candidates_)
                                    if (candidate.min_depth_m_ > 0)
                                        nearest = std::min(nearest,
                                            cv::norm(candidate.pose_.t_m_ - translation));
                                if (std::isfinite(nearest))
                                    csv << nearest;
                            }
                            csv << ',';
                            if (result.T_camera_target_)
                                csv << cv::norm(result.T_camera_target_->t_m_ - translation);
                            csv << '\n';
                            for (std::size_t k = 0; k < result.candidates_.size(); ++k)
                            {
                                const auto &candidate = result.candidates_[k];
                                cv::Matx33d measured_rotation;
                                cv::Rodrigues(candidate.pose_.rvec_, measured_rotation);
                                const double cosine = (cv::trace(rotation.t() * measured_rotation) - 1) / 2;
                                candidates << (sequence - 1) << ',' << k << ','
                                           << candidate.rmse_px_ << ',' << candidate.min_depth_m_
                                           << ',' << cv::norm(candidate.pose_.t_m_ - translation)
                                           << ',' << std::acos(std::clamp(cosine, -1.0, 1.0)) << ','
                                           << candidate.max_error_px_ << ','
                                           << mark::pose::detail::quad_area(observed) << '\n';
                            }
                        }
        if (!csv.good() || !candidates.good())
            throw std::runtime_error("CSV 写入失败");
        cv::FileStorage summary((out / "summary.yaml").string(), cv::FileStorage::WRITE);
        if (!summary.isOpened())
            throw std::runtime_error("摘要打不开");
        summary << "samples" << std::to_string(sequence) << "unique" << std::to_string(unique)
                << "invalid" << std::to_string(invalid) << "seed" << 20261015
                << "hardware_confirmed" << 0
                << "note" << "未经过实车确认；truth_nearest 是离线评分，不是生产选择";
        std::cout << "合成探针=" << sequence << " 唯一=" << unique << " 无效=" << invalid
                  << "；不自动填预算；未经过实车确认\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}