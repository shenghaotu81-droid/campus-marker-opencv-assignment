#include "offline/offline_runner.hpp"
#include "detector/config.hpp"
#include "block3_fixture.hpp"
#include "assertions.hpp"
#include <chrono>
#include <iostream>
#include <opencv2/videoio.hpp>
int main() {
    const auto root = std::filesystem::temp_directory_path() / ("lean-chain-" +
        std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    try {
        const auto source=std::filesystem::path(VISION_SOURCE_ROOT);
        const auto video=root/"input.avi";
        const auto image=fixture::scene().frame.original_image_;
        cv::VideoWriter writer(video.string(),cv::VideoWriter::fourcc('M','J','P','G'),30,image.size());
        test_support::check(writer.isOpened(),"fixture video open failed");
        for(int i=0;i<4;++i) writer.write(i==2 ? cv::Mat::zeros(image.size(),image.type()) : image);
        writer.release();
        auto app=mark::loadConfig(source/"config/detector.yaml");
        app.offline.directory=(root/"run").string();
        test_support::check(mark::runOffline(app,video.string(),(source/"config/detector.yaml").string())==0,"offline chain failed");
        cv::VideoCapture output((root/"run/overlay.mp4").string());
        cv::Mat frame; int count=0;
        while(output.read(frame)) { ++count; test_support::check(frame.size()==image.size(),"overlay dimensions changed"); }
        test_support::check(count==4,"overlay frame count differs");
        cv::FileStorage summary((root/"run/summary.yaml").string(),cv::FileStorage::READ);
        test_support::check(std::string(summary["frames"])=="4","summary count differs");
#ifdef VISION_TEST_TRACKING
        cv::FileStorage poses((root/"run/observations.yaml").string(),cv::FileStorage::READ);
        cv::FileStorage tracks((root/"run/tracks.yaml").string(),cv::FileStorage::READ);
        test_support::check(poses["frames"].size()==4 && tracks["frames"].size()==4,"stages skipped frames");
        for(const auto &f:tracks["frames"]) test_support::check(int(f["input_accepted"])==1,"tracker did not consume a frame");
#endif
        bool rejected=false;
        try { mark::runOffline(app,video.string(),(source/"config/detector.yaml").string()); }
        catch(const std::runtime_error&) { rejected=true; }
        test_support::check(rejected,"output overwrite accepted");
        std::filesystem::remove_all(root);
        return 0;
    } catch(const std::exception &e) {
        std::cerr<<e.what()<<'\n';
        std::filesystem::remove_all(root);
        return 1;
    }
}
