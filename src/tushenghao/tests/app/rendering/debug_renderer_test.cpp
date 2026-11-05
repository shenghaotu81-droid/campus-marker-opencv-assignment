#include "rendering/debug_renderer.hpp"
#include "assertions.hpp"
#include <iostream>
int main() {
    try {
        cv::Mat input(300,400,CV_8UC3,cv::Scalar::all(0));
        mark::FrameResult raw{}; raw.status=mark::Status::DETECTED;
        mark::Detection d{};d.corners={{{80,80},{220,80},{220,220},{80,220}}};
        raw.detections.push_back(d);
        mark::tracking::TargetTrack track;track.track_id_=1;
        track.state_=mark::tracking::TrackState::Coasting;
        track.position_m_=cv::Vec3d(0,0,2);track.velocity_mps_=cv::Vec3d(.2,0,0);
        const auto out=mark::renderDebugFrame(input,raw,{}, {track});
        test_support::check(cv::norm(input)==0 && out.data!=input.data && cv::norm(out)>0,"render changed source or drew nothing");
        test_support::check(raw.detections[0].corners==d.corners && track.state_==mark::tracking::TrackState::Coasting,"render fed back");
        return 0;
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n';return 1; }
}
