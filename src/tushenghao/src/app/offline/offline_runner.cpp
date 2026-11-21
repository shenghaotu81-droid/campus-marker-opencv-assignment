// 逐帧写观测和轨迹，运行结束后写汇总。叠加视频可选。
#include "offline/offline_runner.hpp"
#include "vision/pose/pose_io.hpp"
#include "vision/config/detector_config.hpp"
#include "vision/detection/detector.hpp"
#include "rendering/debug_renderer.hpp"
#include "reporting/run_console.hpp"
#include "export/video_exporter.hpp"
#include "vision/pose/pose_estimator.hpp"
#include "vision/pose/tag_detector.hpp"
#include "vision/tracking/target_tracker.hpp"
#include "io/tracking_io.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <opencv2/videoio.hpp>

namespace mark
{
    int run_offline(const AppConfig &app, const std::string &video,
                    const std::filesystem::path &config_dir)
    {
        // 用单调时钟统计整次运行的处理时间。
        using Clock = std::chrono::steady_clock;

        cv::VideoCapture capture(video);
        if (!capture.isOpened())
            throw std::runtime_error("INPUT_OPEN_FAILED: " + video);
        const double fps = capture.get(cv::CAP_PROP_FPS);
        if (!std::isfinite(fps) || fps <= 0)
            throw std::runtime_error("INPUT_FPS_INVALID");

        detector::Detector detector(app.detector_config_);

        const auto pose_config = pose::load_pose_config(config_dir / "pose.yaml");
        pose::PoseEstimator solver(pose_config);

        std::unique_ptr<pose::TagDetector> tag_detector;
        if (!pose_config.tag_.allowed_ids_.empty())
            tag_detector = std::make_unique<pose::TagDetector>(pose_config.tag_);

        const auto tracking_config = tracking::load_tracking_config(config_dir / "tracking.yaml");
        if (!tracking_config.profile_)
            std::cout << "Tracking profile未配置：跳过跟踪数值计算\n";
        tracking::TargetTracker tracker(tracking_config);

        const std::filesystem::path directory(app.offline_.run_directory_);
        std::filesystem::create_directories(directory);

        VideoExporter exporter;
        RunSummary summary;

        cv::FileStorage observations((directory / "observations.yaml").string(),
                                     cv::FileStorage::WRITE);
        observations << "schema_version" << 6 << "stream_type" << "PoseObservations"
                     << "position_unit" << "m" << "time_unit" << "us" << "frames" << "[";

        cv::FileStorage tracks_file((directory / "tracks.yaml").string(), cv::FileStorage::WRITE);
        tracks_file << "schema_version" << 2 << "stream_type" << "TrackingResult" << "frames"
                    << "[";

        const auto start = Clock::now();

        cv::Mat image_;

        for (uint64_t id = 0; capture.read(image_); ++id)
        {
            // 用帧号和 FPS 推算离线名义时间，四舍五入到整数微秒。
            const long double timestamp = static_cast<long double>(id) * 1e6L / fps;

            detector::FrameInput input{image_, id, std::llround(timestamp)};

            const auto raw = detector.process(input);
            summary.add(raw);

            std::vector<pose::PoseObservation> poses_;
            std::vector<tracking::TargetTrack> tracks_;

            pose::CaptureStamp stamp;

            stamp.frame_id_ = id;
            stamp.received_us_ = input.timestamp_us_;

            // 每帧至少保留一条 MARK 观测。无检测时估计和原因都为空。
            for (size_t i = 0; i < std::max(size_t(1), raw.detections_.size()); ++i)
                poses_.push_back(solver.estimate_mark(raw, i, image_.size(), stamp));

            if (tag_detector)
                for (const auto &tag : tag_detector->detect(image_, stamp))

                    poses_.push_back(pose::estimate_exit(tag, image_.size(), solver));

            // 按观测条数累计位姿状态。启用 Tag 时一帧可有多条观测。
            for (const auto &p : poses_)
                ++summary.poses_[pose::pose_status_name(p)];

            tracking::TrackingFrame frame;
            frame.stamp_ = stamp;
            frame.observations_ = poses_;

            // 缺检测时继续推进已有轨迹。数值失败时，跟踪器保留已提交状态。
            const auto result = tracker.step(frame);

            tracks_ = result.tracks_;

            for (const auto &t : tracks_)

                ++summary.tracks_[tracking::track_state_name(t.state_)];
            for (const auto &event : result.events_)

                ++summary.reasons_[event];

            tracking::write_tracking_result(tracks_file, result);

            observations << "{" << "stamp";
            pose::write_capture_stamp(observations, stamp);
            observations << "observations" << "[";
            for (const auto &p : poses_)
                pose::write_pose_observation(observations, p);
            observations << "]" << "}";

            if (app.offline_.overlay_)
            {
                if (id == 0)
                    exporter.open(directory / app.offline_.video_filename_, image_.size(), fps);

                exporter.write(render_debug_frame(image_, raw, poses_, tracks_, &pose_config));
            }
        }

        // 有叠加帧时关闭编码器，将临时文件改名成配置指定的目标。
        if (app.offline_.overlay_ && summary.frames_)
            exporter.finish();

        observations << "]";
        tracks_file << "]";

        summary.elapsed_s_ = std::chrono::duration<double>(Clock::now() - start).count();

        write_run_summary(summary, (directory / "summary.yaml").string());

        write_run_console(std::cout, summary);

        return 0;
    }
}
