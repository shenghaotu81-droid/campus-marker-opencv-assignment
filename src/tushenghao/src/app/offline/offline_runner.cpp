#include "offline/offline_runner.hpp"
#include "detector/config.hpp"
#include "vision/detection/detector.hpp"
#include "rendering/debug_renderer.hpp"
#include "reporting/run_console.hpp"
#include "export/video_exporter.hpp"
#ifdef VISION_HAS_POSE
#include "vision/pose/pose_estimator.hpp"
#include "vision/pose/tag_detector.hpp"
#endif
#ifdef VISION_HAS_TRACKING
#include "vision/tracking/target_tracker.hpp"
#include "io/tracking_io.hpp"
#endif
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <opencv2/videoio.hpp>

namespace mark {
int runOffline(AppConfig app, const std::string &video, const std::string &config_path) {
    using Clock = std::chrono::steady_clock;
    validateConfig(app);
    const bool camera = video.rfind("camera:", 0) == 0;
    cv::VideoCapture capture;
    if (camera) capture.open(std::stoi(video.substr(7)));
    else capture.open(video);
    if (!capture.isOpened()) throw std::runtime_error("INPUT_OPEN_FAILED: " + video);
    const double fps = capture.get(cv::CAP_PROP_FPS);
    if (!std::isfinite(fps) || fps <= 0) throw std::runtime_error("INPUT_FPS_INVALID");
    Detector detector(app.detector_config);
    const auto config_dir = std::filesystem::path(config_path).parent_path();
    std::optional<pose::G2Config> pose_config;
#ifdef VISION_HAS_POSE
    pose_config = pose::load_pose_config(config_dir / "pose.yaml");
    pose::PoseEstimator solver(*pose_config);
    std::unique_ptr<pose::TagDetector> tag_detector;
    if (!pose_config->tag_.allowed_ids_.empty())
        tag_detector = std::make_unique<pose::TagDetector>(pose_config->tag_);
#endif
#ifdef VISION_HAS_TRACKING
    tracking::TargetTracker tracker(tracking::load_tracking_config(config_dir / "tracking.yaml"));
#endif
    const std::filesystem::path directory(app.offline.directory);
    if (!std::filesystem::create_directories(directory))
        throw std::runtime_error("OUTPUT_DIRECTORY_EXISTS: " + directory.string());
    VideoExporter exporter;
    RunSummary summary;
#ifdef VISION_HAS_POSE
    cv::FileStorage observations((directory / "observations.yaml").string(), cv::FileStorage::WRITE);
    if (!observations.isOpened()) throw std::runtime_error("POSE_OUTPUT_OPEN_FAILED");
#ifdef VISION_HAS_TRACKING
    observations << "schema_version" << 4 << "stream_type" << "G4PoseReplay"
        << "position_unit" << "m" << "time_unit" << "us" << "frames" << "[";
#else
    observations << "schema_version" << 1 << "stream_type" << "G2PoseObservation"
        << "session_id" << video << "camera_id" << pose_config->camera_.camera_id_
        << "clock_domain" << (camera ? "host-monotonic-receive" : "video-fps") << "frames" << "[";
#endif
#endif
#ifdef VISION_HAS_TRACKING
    cv::FileStorage tracks_file((directory / "tracks.yaml").string(), cv::FileStorage::WRITE);
    if (!tracks_file.isOpened()) throw std::runtime_error("TRACK_OUTPUT_OPEN_FAILED");
    tracks_file << "schema_version" << 1 << "stream_type" << "G4TrackingResult" << "frames" << "[";
#endif
    const auto start = Clock::now();
    cv::Mat image;
    for (uint64_t id=0; capture.read(image); ++id) {
        const long double timestamp = camera ?
            std::chrono::duration<long double, std::micro>(Clock::now() - start).count() :
            static_cast<long double>(id) * 1e6L / fps;
        if (!std::isfinite(timestamp) || timestamp > std::numeric_limits<int64_t>::max() - .5L)
            throw std::runtime_error("INPUT_TIMESTAMP_OVERFLOW");
        FrameInput input{image, id, std::llround(timestamp), TimestampSource::Unknown};
        const auto raw = detector.process(input);
        summary.add(raw);
        std::vector<pose::PoseObservation> poses;
        std::vector<tracking::TargetTrack> tracks;
#ifdef VISION_HAS_POSE
        pose::CaptureStamp stamp;
        stamp.session_id_ = video;
        stamp.camera_id_ = pose_config->camera_.camera_id_;
        stamp.clock_domain_ = camera ? "host-monotonic-receive" : "video-fps";
        stamp.pixel_domain_ = pose::PixelDomain::RawDistorted;
        stamp.frame_id_ = id;
        stamp.received_us_ = input.timestamp_us;
        // 录像时间轴与相机接收时刻均不是实测曝光；曝光和时间误差保持未知。
        for (size_t i=0; i<std::max(size_t(1), raw.detections.size()); ++i)
            poses.push_back(solver.estimate_mark(raw, i, image.size(), stamp));
        if (tag_detector)
            for (const auto &tag : tag_detector->detect(image, stamp))
                poses.push_back(pose::estimate_exit(tag, image.size(), solver).pose_);
        for (const auto &p : poses) ++summary.poses[pose::pose_status_name(p.status_)];
#endif
#ifdef VISION_HAS_TRACKING
        tracking::TrackingFrame frame;
        frame.stamp_ = stamp;
        frame.origin_ = camera ? tracking::InputOrigin::TypedMemory : tracking::InputOrigin::Synthetic;
        frame.observations_ = poses;
        const auto result = tracker.step(frame); // 无检测也推进coast/lost。
        tracks = result.tracks_;
        for (const auto &t : tracks) ++summary.tracks[tracking::track_state_name(t.state_)];
        for (const auto &event : result.events_) ++summary.reasons[event];
        tracking::write_tracking_frame(observations, frame);
        tracking::write_tracking_result(tracks_file, result);
#elif defined(VISION_HAS_POSE)
        observations << "{" << "frame_id" << std::to_string(id) << "timestamp_us"
            << std::to_string(stamp.received_us_) << "mark" << "[";
        for (const auto &p : poses) {
            observations << "{" << "source" << p.source_ << "target_id" << p.target_id_
                << "detection_index" << std::to_string(p.detection_index_)
                << "status" << pose::pose_status_name(p.status_) << "control_usable" << int(p.control_usable_)
                << "corner_quality_flags" << int(p.corner_quality_flags_) << "candidates" << "[";
            for (const auto &c : p.candidates_) {
                observations << "{" << "rvec" << cv::Mat(c.pose_.rvec_) << "t_m" << cv::Mat(c.pose_.t_m_)
                    << "metrics_valid" << int(c.metrics_valid_) << "eligible" << int(c.eligible_)
                    << "rmse_px" << c.rmse_px_ << "max_error_px" << c.max_error_px_
                    << "min_depth_m" << c.min_depth_m_ << "reasons" << "[";
                for (const auto &reason : c.reasons_) observations << reason;
                observations << "]" << "}";
            }
            observations << "]";
            if (p.selected_index_) observations << "selected_index" << int(*p.selected_index_);
            if (p.T_camera_target_) observations << "rvec" << cv::Mat(p.T_camera_target_->rvec_)
                << "t_m" << cv::Mat(p.T_camera_target_->t_m_);
            observations << "reasons" << "[";
            for (const auto &reason : p.reasons_) observations << reason;
            observations << "]" << "}";
        }
        observations << "]" << "}";
#endif
        if (!exporter.opened()) exporter.open(directory, image.size(), fps, app.offline.video_fourcc);
        exporter.write(renderDebugFrame(image, raw, poses, tracks, pose_config ? &*pose_config : nullptr));
    }
    if (!summary.frames) throw std::runtime_error("INPUT_HAS_NO_FRAMES");
    exporter.finish(true);
#ifdef VISION_HAS_POSE
    observations << "]";
#endif
#ifdef VISION_HAS_TRACKING
    tracks_file << "]";
#endif
    if (app.detector_config.debug.timing_enabled)
        summary.elapsed_s = std::chrono::duration<double>(Clock::now() - start).count();
    writeRunSummary(summary, (directory / "summary.yaml").string());
    write_run_console(std::cout, summary);
    return 0;
}
} // namespace mark
