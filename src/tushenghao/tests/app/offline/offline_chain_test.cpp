#include "offline/offline_runner.hpp"
#include "detector/config.hpp"
#include "block3_fixture.hpp"
#include "assertions.hpp"
#include <chrono>
#include <iostream>
#include <fstream>
#include <regex>
#include <opencv2/calib3d.hpp>
#ifdef VISION_TEST_TRACKING
#include "vision/config/pose_config.hpp"
#endif
#include <opencv2/videoio.hpp>
#ifdef VISION_TEST_TRACKING
// 解析图纸并从已知透视真值生成像素；预算/profile只在本测试目录，不写回生产配置。
void unique_chain(const std::filesystem::path &root, const std::filesystem::path &source)
{
    const auto config = root / "unique-config";
    std::filesystem::create_directory(config);
    for (const auto &name:{"pose.yaml", "tracking.yaml", "calibration.yaml", "marker_geometry.yaml"})
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
schema_version: 1
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
        cv::Mat image = cv::Mat::zeros(1080, 1440, CV_8UC3);
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
                cv::fillPoly(image, std::vector<std::vector<cv::Point> >{pixels}, {255, 255, 255});
            }
        writer.write(image);
    }
    writer.release();
    auto app = mark::loadConfig(source / "config/detector.yaml");
    app.offline.directory = (root / "unique-run").string();
    mark::runOffline(app, video.string(), (config / "detector.yaml").string());
    cv::FileStorage poses((root / "unique-run/observations.yaml").string(), cv::FileStorage::READ);
    cv::FileStorage tracks((root / "unique-run/tracks.yaml").string(), cv::FileStorage::READ);
    test_support::check(poses["frames"].size() == 24 && tracks["frames"].size() == 24, "perspective frames missing");
    bool velocity = false;
    for (int i = 0; i < 24; ++i)
    {
        const bool missing = i == 13 || i == 14;
        const auto pose = poses["frames"][i]["observations"][0];
        test_support::check(std::string(pose["status"]) == (missing ? "NOT_DETECTED" : "AMBIGUOUS"), "perspective pose wrong");
        const auto track = tracks["frames"][i]["tracks"][0];
        test_support::check(std::string(track["state"]) == (missing ? "COASTING" : "MEASURED"), "G2 to G4 flow/coast broken");
        test_support::check(!track["position_m"].empty() && track["time_bounded"].empty() && track["qualified_for_g5"].empty(), "finite position missing/dead qualification label survived");
        velocity = velocity || !track["velocity_mps"].empty();
    }
    test_support::check(velocity, "full chain never produced apparent velocity");
}

#endif
int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("lean-chain-" +
                                                                std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    try {
        const auto source = std::filesystem::path(VISION_SOURCE_ROOT);
        const auto video = root / "input.avi";
        const auto image = fixture::scene().frame.original_image_;
        cv::VideoWriter writer(video.string(), cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 30, image.size());
        test_support::check(writer.isOpened(), "fixture video open failed");
        for (int i = 0; i < 4; ++i)
            writer.write(i == 2 ? cv::Mat::zeros(image.size(), image.type()) : image);
        writer.release();
        auto app = mark::loadConfig(source / "config/detector.yaml");
        app.offline.directory = (root / "run").string();
        test_support::check(mark::runOffline(app, video.string(), (source / "config/detector.yaml").string()) == 0, "offline chain failed");
        cv::VideoCapture output((root / "run/overlay.avi").string());
        test_support::check(output.isOpened() &&
                            static_cast<int>(output.get(cv::CAP_PROP_FOURCC)) == cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
                            "AVI export silently changed MJPG codec");
        cv::Mat frame;
        int count = 0;
        while (output.read(frame))
        {
            ++count;
            test_support::check(frame.size() == image.size(), "overlay dimensions changed");
        }
        test_support::check(count == 4, "overlay frame count differs");
        cv::FileStorage summary((root / "run/summary.yaml").string(), cv::FileStorage::READ);
        test_support::check(std::string(summary["frames"]) == "4", "summary count differs");
#ifdef VISION_TEST_TRACKING
        cv::FileStorage poses((root / "run/observations.yaml").string(), cv::FileStorage::READ);
        cv::FileStorage tracks((root / "run/tracks.yaml").string(), cv::FileStorage::READ);
        test_support::check(poses["frames"].size() == 4 && tracks["frames"].size() == 4, "stages skipped frames");
        for (const auto &f:tracks["frames"])
            test_support::check(int(f["input_accepted"]) == 1, "tracker did not consume a frame");
#endif
#ifdef VISION_TEST_TRACKING
        unique_chain(root, source);
#endif
        bool rejected = false;
        try {
            mark::runOffline(app, video.string(), (source / "config/detector.yaml").string());
        }
        catch (const std::runtime_error &) {
            rejected = true;
        }
        test_support::check(rejected, "output overwrite accepted");
        std::filesystem::remove_all(root);
        return 0;
    }
    catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        std::filesystem::remove_all(root);
        return 1;
    }
}
