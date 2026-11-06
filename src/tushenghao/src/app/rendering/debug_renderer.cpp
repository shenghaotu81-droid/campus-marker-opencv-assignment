#include "rendering/debug_renderer.hpp"
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <iomanip>
#include <sstream>
namespace mark
{
    cv::Mat renderDebugFrame(const cv::Mat &image, const FrameResult &raw,
                             const std::vector<pose::PoseObservation> &poses, const std::vector<tracking::TargetTrack> &tracks,
                             const pose::G2Config *config)
    {
        cv::Mat out = image.clone();
        auto text = [&](const std::string &s, int row, cv::Scalar color)
                    {
                        cv::putText(out, s, {12, 24 + row * 24}, cv::FONT_HERSHEY_SIMPLEX, .55, color, 1, cv::LINE_AA);
                    };
        text("frame " + std::to_string(raw.frame_id), 0, {255, 255, 255});
        for (const auto &d : raw.detections)
            for (int i = 0; i < 4; ++i)
            {
                cv::line(out, d.corners[i], d.corners[(i + 1) % 4], {0, 255, 0}, 1, cv::LINE_AA);
                cv::circle(out, d.corners[i], 3, {0, 255, 0}, -1);
                cv::putText(out, std::to_string(i), d.corners[i], cv::FONT_HERSHEY_SIMPLEX, .5, {0, 255, 0});
            }
        int row = 1;
        for (const auto &p : poses)
        {
            // 投影只使用当前有效位姿；不会把预测点作为新角点反馈给检测器。
            text(p.source_ + ": " + std::string(p.status_ == pose::PoseStatus::Unique ? "Unique" :
                                                p.status_ == pose::PoseStatus::Ambiguous ? "Nominal (two solutions)" : "No pose"), row++, {255, 255, 0});
            if (!config || !p.measurement() || p.source_ != "MARK")
                continue;
            std::vector<cv::Point2d> pixels;
            cv::projectPoints(std::vector<cv::Point3d>(config->mark_points_m_.begin(), config->mark_points_m_.end()),
                              p.measurement()->pose_.rvec_, p.measurement()->pose_.t_m_, config->camera_.K_, config->camera_.dist_, pixels);
            for (int i = 0; i < 4; ++i)
                cv::line(out, pixels[i], pixels[(i + 1) % 4], {255, 255, 0}, 1, cv::LINE_AA);
        }
        for (const auto &t : tracks)
        {
            std::ostringstream label;
            label << "track " << t.track_id_ << (t.state_ == tracking::TrackState::Coasting ? " COAST" :
                                                 " MEASURED") << std::fixed << std::setprecision(3);
            if (t.position_m_)
                label << " p(m)=" << *t.position_m_;
            if (t.velocity_mps_)
                label << " v(m/s)=" << *t.velocity_mps_;
            text(label.str(), row++, {0, 180, 255});
            // 轨迹均为相机坐标；只有可投影位置才画点/速度箭头。
            if (!config || !t.position_m_ || (*t.position_m_)[2] <= 0)
                continue;
            const auto &p = *t.position_m_;
            std::vector<cv::Point3d> points{{p[0], p[1], p[2]}};
            if (t.velocity_mps_)
            {
                const auto end = p + *t.velocity_mps_ * .1; // 箭头表示100ms表观位移，仅作显示。
                if (end[2] > 0)
                    points.push_back({end[0], end[1], end[2]});
            }
            std::vector<cv::Point2d> pixels;
            cv::projectPoints(points, cv::Vec3d(), cv::Vec3d(), config->camera_.K_, config->camera_.dist_, pixels);
            if (!cv::checkRange(cv::Mat(pixels)))
                continue;
            cv::circle(out, pixels[0], 5, {0, 180, 255}, 2);
            if (pixels.size() == 2)
                cv::arrowedLine(out, pixels[0], pixels[1], {0, 180, 255}, 2, cv::LINE_AA);
        }
        return out;
    }

}
