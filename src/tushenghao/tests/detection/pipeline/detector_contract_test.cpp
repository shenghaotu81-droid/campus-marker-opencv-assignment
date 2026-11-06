#include "vision/detection/detector.hpp"
#include "detector/config.hpp"
#include "block3_fixture.hpp"
#include "assertions.hpp"
#include <iostream>
int main()
{
    try{
        using test_support::check;
        mark::Detector detector(mark::loadConfig(std::filesystem::path(VISION_SOURCE_ROOT) / "config/detector.yaml").detector_config);
        auto scene = fixture::scene();
        auto image = scene.frame.original_image_;
        auto before = image.clone();
        auto first = detector.process({image, 0, 0});
        check(first.status == mark::Status::DETECTED && first.detections.size() == 1, "valid scene lost");
        check(cv::norm(image, before) == 0, "detector edited source");
        check(detector.process({image, 0, 1}).status == mark::Status::INVALID_INPUT, "duplicate accepted");
        check(detector.process({image, 1, 2}).status == mark::Status::DETECTED, "failed recovery");
        detector.reset();
        check(detector.process({{}, 0, 0}).status == mark::Status::INVALID_INPUT, "empty input");
        check(detector.process({cv::Mat(20, 20, CV_8UC1), 0, 0}).status == mark::Status::INVALID_INPUT, "gray input");
        check(detector.process({cv::Mat::zeros(image.size(), image.type()), 0, 0}).status == mark::Status::NOT_DETECTED, "black became MARK");
        return 0;
    }
    catch (const std::exception &e){
        std::cerr << e.what() << '\n';
        return 1;
    }
}
