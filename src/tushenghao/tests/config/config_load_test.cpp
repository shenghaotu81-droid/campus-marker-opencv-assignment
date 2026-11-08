#include "vision/config/app_config.hpp"
#include "vision/config/pose_config.hpp"
#include "vision/config/tracking_config.hpp"
#include "vision/config/cli_options.hpp"
#include "assertions.hpp"
#include <iostream>

int main()
{
    try
    {
        using test_support::check;
        const auto directory = std::filesystem::path(VISION_SOURCE_ROOT) / "config";
        const auto detector = mark::detector::load_detector_config(directory / "detector.yaml");
        check(detector.corner_.physical_to_screen_ == std::array<int, 4>{{0,1,2,3}}, "fixed mapping");
        const auto pose = mark::pose::load_pose_config(directory / "pose.yaml");
        const std::array<cv::Point3d, 4> expected{{{0,0,0}, {.08,0,0}, {.08,.08,0}, {0,.08,0}}};
        check(pose.mark_points_m_ == expected, "metric model changed");
        const auto tracking = mark::tracking::load_tracking_config(directory / "tracking.yaml");
        check(tracking.profile_.has_value() && tracking.profile_->camera_mse_m2_(2,2) == 2.5e-7,
              "profile path not loaded");
        const auto offline = mark::load_offline_config(directory / "offline.yaml");
        check(offline.overlay_ && offline.video_filename_ == "overlay.avi", "offline options lost");
        // 终端误输仍须给出明确错误，不把选项名当作视频路径。
        for (const auto &tokens : std::vector<std::vector<std::string>>{
                {"app", "--camera", "0"}, {"app", "--config", "config.yaml"},
                {"app", "--video"}, {"app", "--video", "--video"},
                {"app", "--video", "a.avi", "--video", "b.avi"}})
        {
            auto copy = tokens;
            std::vector<char *> argv;
            for (auto &token : copy) argv.push_back(token.data());
            bool failed = false;
            try { mark::parse_options(static_cast<int>(argv.size()), argv.data()); }
            catch (const std::runtime_error &) { failed = true; }
            check(failed, "bad terminal input accepted");
        }
        return 0;
    }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
