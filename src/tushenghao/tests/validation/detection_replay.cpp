#include "vision/detection/detector.hpp"
#include "detector/config.hpp"
#include <opencv2/videoio.hpp>
#include <fstream>
#include <iomanip>
#include <iostream>
int main(int argc, char **argv)
{
    try{
        if (argc != 4)
            return 1;
        mark::Detector detector(mark::loadConfig(argv[1]).detector_config);
        cv::VideoCapture video(argv[2]);
        if (!video.isOpened())
            return 1;
        std::ofstream out(argv[3]);
        if (!out)
            return 1;
        out << std::setprecision(17);
        cv::Mat image;
        uint64_t id = 0, detected = 0, corners = 0;
        double fps = video.get(cv::CAP_PROP_FPS);
        while (video.read(image))
        {
            auto r = detector.process({image, id, std::llround(id * 1e6 / fps)});
            out << id << ' ' << r.detections.size();
            for (const auto &d:r.detections)
            {
#ifdef BASELINE
                auto mapping = *d.attributes.orientation;
#else
                auto mapping = d.orientation;
#endif
                for (int i = 0; i < 4; ++i)
                {
                    auto p = d.corners[mapping[i]];
                    out << ' ' << p.x << ' ' << p.y;
                }
                ++detected;
                corners += 4;
            }
            out << '\n';
            ++id;
        }
        std::cout << "frames=" << id << " detected=" << detected << " corners=" << corners << '\n';
        return id ? 0 : 1;
    }
    catch (const std::exception &e){
        std::cerr << e.what() << '\n';
        return 1;
    }
}
