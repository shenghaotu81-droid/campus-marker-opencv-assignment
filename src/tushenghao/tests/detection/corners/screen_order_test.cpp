#include "vision/types/corner_types.hpp"
#include "decode_stage.hpp"
#include "assertions.hpp"
#include <algorithm>
#include <iostream>
int main()
{
    try{
        using test_support::check;
        for (double degrees:{-10., 0., 10.})
        {
            mark::detector::CornerPoints q{{{0, 0}, {100, 0}, {100, 80}, {0, 80}}};
            double a = degrees * CV_PI / 180;
            for (auto &p:q)
                p = {150 + std::cos(a) * p.x - std::sin(a) * p.y, 150 + std::sin(a) * p.x + std::cos(a) * p.y};
            for (int start = 0; start < 4; ++start)
            {
                auto input = q;
                std::rotate(input.begin(), input.begin() + start, input.end());
                std::string why;
                auto sorted = mark::detector::order_screen_corners(input, why);
                check(sorted && *sorted == q, "shake changed screen slots");
            }
        }
        std::string why;
        mark::detector::CornerPoints duplicate{{{0, 0}, {0, 0}, {10, 10}, {0, 10}}};
        check(!mark::detector::order_screen_corners(duplicate, why), "duplicate accepted");
        mark::detector::CornerPoints roundoff{{{100, 100}, {100 + 1e-6, 100}, {200, 200}, {100, 200}}};
        check(!mark::detector::publish_float_detection(roundoff, {300, 300}, {}, why), "float collision accepted");
        mark::detector::CornerPoints boundary{{{10, 10}, {300 - 1e-7, 10}, {299, 200}, {10, 200}}};
        check(!mark::detector::publish_float_detection(boundary, {300, 300}, {}, why), "float out of image accepted");
        return 0;
    }
    catch (const std::exception &e){
        std::cerr << e.what() << '\n';
        return 1;
    }
}
