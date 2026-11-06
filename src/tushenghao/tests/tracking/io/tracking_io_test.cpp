#include "io/tracking_io.hpp"
#include "assertions.hpp"
#include <iostream>
int main()
{
    try{
        mark::tracking::TrackingResult f;
        f.input_stamp_.frame_id_ = uint64_t(1) << 60;
        f.input_stamp_.received_us_ = int64_t(1) << 60;
        cv::FileStorage out("memory.yaml", cv::FileStorage::WRITE | cv::FileStorage::MEMORY);
        out << "frames" << "[";
        mark::tracking::write_tracking_result(out, f);
        out << "]";
        auto text = out.releaseAndGetString();
        cv::FileStorage in(text, cv::FileStorage::READ | cv::FileStorage::MEMORY);
        auto stamp = in["frames"][0]["stamp"];
        test_support::check(std::string(stamp["frame_id"]) == std::to_string(f.input_stamp_.frame_id_) && std::string(stamp["received_us"]) == std::to_string(f.input_stamp_.received_us_), "64-bit stamp narrowed");
        test_support::check(in["frames"][0]["origin"].empty(), "test-only origin survived");
        return 0;
    }
    catch (const std::exception &e){
        std::cerr << e.what() << '\n';
        return 1;
    }
}
