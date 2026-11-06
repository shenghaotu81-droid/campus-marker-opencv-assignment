// 只读IPPE计数：直接调用与G2相同的求解器/数学复核，不向生产插入trace。
#include "vision/detection/detector.hpp"
#include "vision/config/pose_config.hpp"
#include "detector/config.hpp"
#include "validation/pose_geometry.hpp"
#include <opencv2/calib3d.hpp>
#include <opencv2/videoio.hpp>
#include <cmath>
#include <vector>
#include <fstream>
#include <iostream>
#include <map>
int main(int argc, char **argv)
{
    try
    {
        if (argc != 4) return 1;
        const std::filesystem::path config(argv[1]);
        mark::Detector detector(mark::loadConfig(config).detector_config);
        const auto pose = mark::pose::load_pose_config(config.parent_path() / "pose.yaml");
        cv::VideoCapture video(argv[2]);
        std::ofstream out(argv[3]);
        if (!video.isOpened() || !out) return 1;
        const double fps = video.get(cv::CAP_PROP_FPS);
        cv::Mat image;
        uint64_t frame = 0, detections = 0;
        std::map<size_t, size_t> raw_counts, legal_counts;
        while (video.read(image))
        {
            auto observed = detector.process({image, frame, std::llround(frame * 1e6 / fps)});
            for (const auto &d : observed.detections)
            {
                std::array<cv::Point2d, 4> pixels;
                for (size_t i = 0; i < 4; ++i) pixels[i] = d.corners[d.orientation[i]];
                std::vector<cv::Mat> rotations, translations;
                cv::solvePnPGeneric(std::vector<cv::Point3d>(pose.mark_points_m_.begin(), pose.mark_points_m_.end()),
                    std::vector<cv::Point2d>(pixels.begin(), pixels.end()), pose.camera_.K_, pose.camera_.dist_,
                    rotations, translations, false, cv::SOLVEPNP_IPPE);
                size_t legal = 0;
                for (size_t i = 0; i < rotations.size(); ++i)
                {
                    mark::pose::RigidPose p;
                    for (int k = 0; k < 3; ++k)
                    {
                        p.rvec_[k] = rotations[i].at<double>(k);
                        p.t_m_[k] = translations[i].at<double>(k);
                    }
                    legal += bool(mark::pose::detail::evaluate_pose(p, pose.mark_points_m_, pixels,
                        pose.camera_.K_, pose.camera_.dist_));
                }
                out << frame << ' ' << rotations.size() << ' ' << legal << '\n';
                ++detections; ++raw_counts[rotations.size()]; ++legal_counts[legal];
            }
            ++frame;
        }
        std::cout << "opencv=" << CV_VERSION << " frames=" << frame << " detections=" << detections << '\n';
        for (auto [count, n] : raw_counts) std::cout << "raw_solutions=" << count << " detections=" << n << '\n';
        for (auto [count, n] : legal_counts) std::cout << "legal_solutions=" << count << " detections=" << n << '\n';
        return frame ? 0 : 1;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
