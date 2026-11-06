#include "rendering/debug_renderer.hpp"
#include "assertions.hpp"
#include <iostream>
int main()
{
    try{
        cv::Mat image = cv::Mat::zeros(240, 320, CV_8UC3), before = image.clone();
        mark::FrameResult raw;
        raw.status = mark::Status::NOT_DETECTED;
        auto out = mark::renderDebugFrame(image, raw, {}, {});
        test_support::check(cv::norm(image, before) == 0 && out.size() == image.size() && cv::norm(out, before) > 0, "renderer mutated input/no output");
        return 0;
    }
    catch (const std::exception &e){
        std::cerr << e.what() << '\n';
        return 1;
    }
}
