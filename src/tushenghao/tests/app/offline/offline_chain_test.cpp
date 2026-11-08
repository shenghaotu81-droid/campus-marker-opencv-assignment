#include "offline/offline_runner.hpp"
#include "vision/config/detector_config.hpp"
#include "block3_fixture.hpp"
#include "assertions.hpp"
#include <chrono>
#include <iostream>
#include <fstream>
#include <regex>
#include <opencv2/calib3d.hpp>
#include "vision/config/pose_config.hpp"
#include <opencv2/videoio.hpp>
// 解析图纸并从已知透视真值生成像素；预算/profile只在本测试目录，不写回生产配置。
void unique_chain(const std::filesystem::path &root, const std::filesystem::path &source)
{
    const auto config = root / "unique-config";
    std::filesystem::create_directory(config);
    for (const auto &name:{"pose.yaml", "tracking.yaml", "calibration.yaml"})
        std::filesystem::copy_file(source / "config" / name, config / name);
    auto replace = [&](const char *name, const char *key, const char *value)
                   {
                       const auto path = config / name;
                       std::ifstream in(path);
                       std::string text((std::istreambuf_iterator<char>(in)), {});
                       in.close();
                       text = std::regex_replace(text, std::regex(std::string(key) + ":[^\\n]*"), std::string(key) + ": " + value);
                       std::ofstream out(path);
                       out << text;
                   };
    replace("tracking.yaml", "profile", "profile.yaml");
    {
        std::ofstream profile(config / "profile.yaml");
        profile << R"(%YAML:1.0
camera_mse_m2: !!opencv-matrix
  rows: 3
  cols: 3
  dt: d
  data: [0.0001, 0., 0., 0., 0.0001, 0., 0., 0., 0.0001]
acceleration_psd_m2ps3: 0.1
initial_velocity_variance_m2ps2: 4.
)";
    }
    const auto camera = mark::pose::load_pose_config(config / "pose.yaml");
    const auto model = fixture::scene().geometry;
    const auto video = root / "unique.avi";
    cv::VideoWriter writer(video.string(), cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 30, {1440, 1080});
    test_support::check(writer.isOpened(), "perspective fixture video failed");
    for (int i = 0; i < 24; ++i)
    {
        cv::Mat image_ = cv::Mat::zeros(1080, 1440, CV_8UC3);
        if (i != 13 && i != 14)
            for (const auto &polygon:model.polygons)
            {
                std::vector<cv::Point3d> object;
                for (const auto &p:polygon.vertices)
                    object.push_back({p.x * .001, p.y * .001, 0});
                std::vector<cv::Point2d> projected;
                cv::projectPoints(object, cv::Vec3d(.15, -.1, .05), cv::Vec3d(-.04 + i * .0001, -.04, .55),
                                  camera.camera_.K_, camera.camera_.dist_, projected);
                std::vector<cv::Point> pixels;
                for (const auto &p:projected)
                    pixels.emplace_back(cvRound(p.x), cvRound(p.y));
                cv::fillPoly(image_, std::vector<std::vector<cv::Point> >{pixels}, {255, 255, 255});
            }
        writer.write(image_);
    }
    writer.release();
    mark::AppConfig app{mark::detector::load_detector_config(source / "config/detector.yaml"),
                        mark::load_offline_config(source / "config/offline.yaml")};
    app.offline_.run_directory_ = (root / "pose-run").string();
    mark::run_offline(app, video.string(), config);
    cv::FileStorage poses_((root / "pose-run/observations.yaml").string(), cv::FileStorage::READ);
    cv::FileStorage tracks_((root / "pose-run/tracks.yaml").string(), cv::FileStorage::READ);
    test_support::check(poses_["frames"].size() == 24 && tracks_["frames"].size() == 24, "perspective frames missing");
    bool velocity = false;
    for (int i = 0; i < 24; ++i)
    {
        const bool missing = i == 13 || i == 14;
        const auto pose = poses_["frames"][i]["observations"][0];
        test_support::check(std::string(pose["status"]) == (missing ? "UNAVAILABLE" : "ESTIMATED"), "perspective pose wrong");
        const auto track = tracks_["frames"][i]["tracks"][0];
        test_support::check(std::string(track["state"]) == (missing ? "COASTING" : "MEASURED"), "Pose to Tracking flow/coast broken");
        test_support::check(!track["position_m"].empty() && track["time_bounded"].empty() && track["qualified_for_g5"].empty(), "finite position missing/dead qualification label survived");
        velocity = velocity || !track["velocity_mps"].empty();
    }
    test_support::check(velocity, "full chain never produced apparent velocity");
}

int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("lean-chain-" +
                                                                std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    try {
        const auto source = std::filesystem::path(VISION_SOURCE_ROOT);
        const auto video = root / "input.avi";
        const auto image_ = fixture::scene().frame.original_image_;
        cv::VideoWriter writer(video.string(), cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 30, image_.size());
        test_support::check(writer.isOpened(), "fixture video open failed");
        for (int i = 0; i < 4; ++i)
            writer.write(i == 2 ? cv::Mat::zeros(image_.size(), image_.type()) : image_);
        writer.release();
        mark::AppConfig app{mark::detector::load_detector_config(source / "config/detector.yaml"),
                        mark::load_offline_config(source / "config/offline.yaml")};
        app.offline_.run_directory_ = (root / "run").string();
        test_support::check(mark::run_offline(app, video.string(), source / "config") == 0, "offline chain failed");
        cv::VideoCapture output((root / "run/overlay.avi").string());
        test_support::check(output.isOpened() &&
                            static_cast<int>(output.get(cv::CAP_PROP_FOURCC)) == cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
                            "AVI export silently changed MJPG codec");
        cv::Mat frame;
        int count = 0;
        while (output.read(frame))
        {
            ++count;
            test_support::check(frame.size() == image_.size(), "overlay dimensions changed");
        }
        test_support::check(count == 4, "overlay frame count differs");
        cv::FileStorage summary((root / "run/summary.yaml").string(), cv::FileStorage::READ);
        test_support::check(std::string(summary["frames"]) == "4", "summary count differs");
        cv::FileStorage poses_((root / "run/observations.yaml").string(), cv::FileStorage::READ);
        cv::FileStorage tracks_((root / "run/tracks.yaml").string(), cv::FileStorage::READ);
        test_support::check(poses_["frames"].size() == 4 && tracks_["frames"].size() == 4, "stages skipped frames");
        test_support::check(std::string(poses_["stream_type"]) == "PoseObservations" &&
                            std::string(tracks_["stream_type"]) == "TrackingResult", "stream protocol name differs");
        for (const auto &f:tracks_["frames"])
            test_support::check(int(f["input_accepted"]) == 1, "tracker did not consume a frame");
        unique_chain(root, source);
        // 同目录可重复运行；视频文件名由离线配置决定。
        app.offline_.video_filename_ = "renamed.avi";
        mark::run_offline(app, video.string(), source / "config");
        test_support::check(std::filesystem::exists(root / "run/renamed.avi"), "video filename ignored");
        // 从 YAML 加载 false，确认调用方跳过编码和渲染，数据流继续落盘。
        const auto no_overlay = root / "offline.yaml";
        {
            std::ofstream out(no_overlay);
            out << "%YAML:1.0\n---\noverlay: false\n";
        }
        app.offline_ = mark::load_offline_config(no_overlay);
        app.offline_.run_directory_ = (root / "no-overlay").string();
        mark::run_offline(app, video.string(), source / "config");
        test_support::check(!std::filesystem::exists(root / "no-overlay/overlay.avi"), "disabled overlay written");
        for (const auto &name : {"observations.yaml", "tracks.yaml"})
        {
            std::ifstream enabled(root / "run" / name), disabled(root / "no-overlay" / name);
            test_support::check(std::string((std::istreambuf_iterator<char>(enabled)), {}) ==
                                std::string((std::istreambuf_iterator<char>(disabled)), {}),
                                "overlay switch changed pose or tracking");
        }
        std::filesystem::remove_all(root);
        return 0;
    }
    catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        std::filesystem::remove_all(root);
        return 1;
    }
}
