#include "assertions.hpp"
#include "case_runner.hpp"
#include "block3_fixture.hpp"
#include "reader/config_error.hpp"
#include "detector/corner_budget_validation.hpp"
#include <limits>
namespace {
using test_support::check;
auto measure(const fixture::Scene &s,mark::CornerConfig c=fixture::cornerConfig()) {
    // Missing configuration must be diagnosed before dereferencing the optional.
    mark::validateCornerObservationBudget(c);
    return fixture::resolve(s,c);
}
void direct() {
    auto s=fixture::scene();auto before=s.frame.original_image_.clone();auto r=measure(s);
    check(bool(r.measurement_),r.rejection_reason_.c_str());
    for(int i=0;i<4;++i){const auto &e=r.measurement_->evidence_[i];
        check(cv::norm(r.measurement_->physical_corners_[i]-s.quad[i])<1,"wrong physical corner");
        check(e.original_observation_ && e.frame_id_==7 &&
              e.original_support_arcs_[0].size()>=3 && e.original_support_arcs_[1].size()>=3,"missing evidence");}
    check(cv::norm(before,s.frame.original_image_,cv::NORM_INF)==0,"modified input");
}
void scale() {
    auto baseline=measure(fixture::scene());
    for(auto size:{cv::Size(480,360),cv::Size(960,720),cv::Size(960,360)}){
        auto r=measure(fixture::scene(size.width,size.height));check(bool(r.measurement_),r.rejection_reason_.c_str());
        check(r.measurement_->physical_corners_==baseline.measurement_->physical_corners_,"work scale changed original fit");}
}
void currentPixels() {
    auto s=fixture::scene();check(bool(measure(s).measurement_),"first failed");++s.frame.frame_id_;
    s.frame.original_image_=cv::Mat::zeros(s.frame.original_image_.size(),CV_8UC3);
    check(!measure(s).measurement_,"black current image used old support");
}
void invalidInput() {
    auto s=fixture::scene();s.quad[0].x=std::numeric_limits<double>::quiet_NaN();
    check(!measure(s).measurement_,"nonfinite quad accepted");s=fixture::scene();s.frame.scale_x_=0;
    check(!measure(s).measurement_,"invalid mapping accepted");s=fixture::scene();s.quad[1]=s.quad[0];
    check(!measure(s).measurement_,"degenerate quad accepted");
}
void frameBinding() {
    auto s=fixture::scene();auto c=fixture::cornerConfig();mark::OriginalContourIndex index(s.frame);
    auto other=s.frame;++other.frame_id_;
    auto r=mark::resolveObservedCorners(other,s.quad,c,index);
    check(r.rejection_reason_=="ORIGINAL_INDEX_FRAME_MISMATCH","frame mismatch accepted");
}
void border() {
    auto s=fixture::scene();cv::Mat moved;cv::Mat shift=(cv::Mat_<double>(2,3)<<1,0,-920,0,1,0);
    cv::warpAffine(s.frame.original_image_,moved,shift,s.frame.original_image_.size());s.frame.original_image_=moved;s.frame.image_=moved;
    for(auto &p:s.quad)p.x-=920;check(!measure(s).measurement_,"border support accepted");
}
void shortSupport(){auto c=fixture::cornerConfig();c.min_line_points_=1000;check(!measure(fixture::scene(),c).measurement_,"short support accepted");}
void missingBudget(){auto c=fixture::cornerConfig();c.observation_budget_.reset();bool threw=false;try{measure(fixture::scene(),c);}catch(const mark::ConfigError&){threw=true;}check(threw,"missing budget ignored");}
}
int main(){return test_support::run({{"DirectOriginalCorner",direct},{"OriginalScale",scale},{"CurrentPixels",currentPixels},{"InvalidInput",invalidInput},{"FrameBinding",frameBinding},{"Border",border},{"ShortSupport",shortSupport},{"MissingBudget",missingBudget}});}
