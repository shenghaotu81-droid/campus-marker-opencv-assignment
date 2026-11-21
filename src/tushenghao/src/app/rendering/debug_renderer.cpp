// 叠加检测角点、位姿重投影和跟踪结果，保留原图。
#include "rendering/debug_renderer.hpp"
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <iomanip>
#include <sstream>

namespace mark
{
    cv::Mat render_debug_frame(const cv::Mat &image_, const detector::FrameResult &raw,
                               const std::vector<pose::PoseObservation> &poses_,
                               const std::vector<tracking::TargetTrack> &tracks_,
                               const pose::PoseConfig *config)
    {
        // 复制原图作为画布。
        cv::Mat out = image_.clone();

        auto text = [&](const std::string &s, int row, cv::Scalar color)
        {
            cv::putText(out, s, {12, 24 + row * 24}, cv::FONT_HERSHEY_SIMPLEX, 0.55, color, 1,
                        cv::LINE_AA);
        };

        text("frame " + std::to_string(raw.frame_id_), 0, {255, 255, 255});

        // 用绿框标出检测角点与屏幕索引。
        for (const auto &d : raw.detections_)
            for (int i = 0; i < 4; ++i)
            {
                cv::line(out, d.corners_[i], d.corners_[(i + 1) % 4], {0, 255, 0}, 1, cv::LINE_AA);

                cv::circle(out, d.corners_[i], 3, {0, 255, 0}, -1);

                cv::putText(out, std::to_string(i), d.corners_[i], cv::FONT_HERSHEY_SIMPLEX, 0.5,
                            {0, 255, 0});
            }

        int row = 1;
        // 青框表示 MARK 名义位姿对本帧检测像点的重投影拟合。
        for (const auto &p : poses_)
        {
            text(p.source_ + ": " + pose::pose_status_name(p), row++, {255, 255, 0});

            if (!config || !p.estimate_ || p.source_ != "MARK")
                continue;

            std::vector<cv::Point2d> pixels;

            cv::projectPoints(std::vector<cv::Point3d>(config->mark_points_m_.begin(),
                                                       config->mark_points_m_.end()),
                              p.estimate_->pose_.rvec_, p.estimate_->pose_.t_m_, config->camera_.K_,
                              config->camera_.dist_, pixels);
            for (int i = 0; i < 4; ++i)
                cv::line(out, pixels[i], pixels[(i + 1) % 4], {255, 255, 0}, 1, cv::LINE_AA);
        }

        // 橙色图层显示跟踪位置、状态和可用速度。
        for (const auto &t : tracks_)
        {
            std::ostringstream label;
            label << "track"
                  << (t.state_ == tracking::TrackState::Coasting ? " COAST" : " MEASURED")
                  << std::fixed << std::setprecision(3);
            label << " p(m)=" << t.position_m_;
            if (t.velocity_mps_)
                label << " v(m/s)=" << *t.velocity_mps_;
            text(label.str(), row++, {0, 180, 255});

            if (!config || t.position_m_[2] <= 0)
                continue;

            const auto &p = t.position_m_;
            std::vector<cv::Point3d> points{{p[0], p[1], p[2]}};
            if (t.velocity_mps_)
            {
                // 箭头长度对应 100 ms 表观位移，只用于显示。
                const auto end = p + *t.velocity_mps_ * .1;
                if (end[2] > 0)
                    points.push_back({end[0], end[1], end[2]});
            }

            std::vector<cv::Point2d> pixels;

            // 位置已在相机坐标系，投影使用单位旋转和零平移。
            cv::projectPoints(points, cv::Vec3d(), cv::Vec3d(), config->camera_.K_,
                              config->camera_.dist_, pixels);

            if (!cv::checkRange(cv::Mat(pixels)))
                continue;

            cv::circle(out, pixels[0], 5, {0, 180, 255}, 2);

            if (pixels.size() == 2)
                cv::arrowedLine(out, pixels[0], pixels[1], {0, 180, 255}, 2, cv::LINE_AA);
        }

        return out;
    }
}
