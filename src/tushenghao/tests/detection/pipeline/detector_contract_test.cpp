#include "vision/detection/detector.hpp"
#include "vision/config/detector_config.hpp"
#include "block3_fixture.hpp"
#include "assertions.hpp"
#include <iostream>
int main()
{
    try{
        using test_support::check;
        mark::detector::Detector detector(mark::detector::load_detector_config(std::filesystem::path(VISION_SOURCE_ROOT) / "config/detector.yaml"));
        auto scene = fixture::scene();
        auto image_ = scene.frame.original_image_;
        auto before = image_.clone();
        auto first = detector.process({image_, 0, 0});
        check(first.status_ == mark::detector::Status::DETECTED && first.detections_.size() == 1, "valid scene lost");
        check(cv::norm(image_, before) == 0, "detector edited source");
        check(detector.process({image_, 0, 1}).status_ == mark::detector::Status::INVALID_INPUT, "duplicate accepted");
        check(detector.process({image_, 1, 2}).status_ == mark::detector::Status::DETECTED, "failed recovery");
        detector.reset();
        check(detector.process({{}, 0, 0}).status_ == mark::detector::Status::INVALID_INPUT, "empty input");
        check(detector.process({cv::Mat(20, 20, CV_8UC1), 0, 0}).status_ == mark::detector::Status::INVALID_INPUT, "gray input");
        check(detector.process({cv::Mat::zeros(image_.size(), image_.type()), 0, 0}).status_ == mark::detector::Status::NOT_DETECTED, "black became MARK");
        return 0;
    }
    catch (const std::exception &e){
        std::cerr << e.what() << '\n';
        return 1;
    }
}
