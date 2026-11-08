#include "vision/detection/detector.hpp"
#include "vision/config/detector_config.hpp"
#include <opencv2/videoio.hpp>
#include <fstream>
#include <iomanip>
#include <iostream>
int main(int argc, char **argv)
{
    try{
        if (argc != 4)
            return 1;
        mark::detector::Detector detector(mark::detector::load_detector_config(argv[1]));
        cv::VideoCapture video(argv[2]);
        if (!video.isOpened())
            return 1;
        std::ofstream out(argv[3]);
        if (!out)
            return 1;
        out << std::setprecision(17);
        cv::Mat image_;
        uint64_t id = 0, detected = 0, corners_ = 0;
        double fps = video.get(cv::CAP_PROP_FPS);
        while (video.read(image_))
        {
            auto r = detector.process({image_, id, std::llround(id * 1e6 / fps)});
            out << id << ' ' << r.detections_.size();
            for (const auto &d:r.detections_)
            {
                auto mapping = d.orientation_;
                for (int i = 0; i < 4; ++i)
                {
                    auto p = d.corners_[mapping[i]];
                    out << ' ' << p.x << ' ' << p.y;
                }
                ++detected;
                corners_ += 4;
            }
            out << '\n';
            ++id;
        }
        std::cout << "frames=" << id << " detected=" << detected << " corners=" << corners_ << '\n';
        return id ? 0 : 1;
    }
    catch (const std::exception &e){
        std::cerr << e.what() << '\n';
        return 1;
    }
}
