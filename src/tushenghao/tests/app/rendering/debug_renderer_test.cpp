#include "rendering/debug_renderer.hpp"
#include "assertions.hpp"
#include <iostream>
int main()
{
    try{
        cv::Mat image_ = cv::Mat::zeros(240, 320, CV_8UC3), before = image_.clone();
        mark::detector::FrameResult raw;
        raw.status_ = mark::detector::Status::NOT_DETECTED;
        auto out = mark::render_debug_frame(image_, raw, {}, {});
        test_support::check(cv::norm(image_, before) == 0 && out.size() == image_.size() && cv::norm(out, before) > 0, "renderer mutated input/no output");
        return 0;
    }
    catch (const std::exception &e){
        std::cerr << e.what() << '\n';
        return 1;
    }
}
