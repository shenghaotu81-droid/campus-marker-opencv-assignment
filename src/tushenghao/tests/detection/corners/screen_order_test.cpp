#include "assertions.hpp"
#include "vision/types/detection/corner_types.hpp"
#include "vision/config/detector_config.hpp"
#include "detector/config.hpp"
#include <algorithm>
#include <iostream>
int main(){try{
    using test_support::check;
    mark::CornerConfig c;
    for(double angle:{-10.,0.,10.}) {
        std::array<cv::Point2d,4> q{{{0,0},{100,0},{100,80},{0,80}}};
        double a=angle*CV_PI/180;
        for(auto &p:q)p={300+std::cos(a)*p.x-std::sin(a)*p.y,200+std::sin(a)*p.x+std::cos(a)*p.y};
        for(int start=0;start<4;++start){auto ring=q;std::rotate(ring.begin(),ring.begin()+start,ring.end());
            auto r=mark::orderScreenCorners(ring,c);check(bool(r.screen_order_),"shake rejected");
            check(r.screen_order_->screen_points_==q,"small shake changed screen slots");
            check(r.screen_order_->physical_to_screen_==c.physical_to_screen_,"mapping inferred from input");}
    }
    std::array<cv::Point2d,4> q{{{0,0},{100,0},{100,80},{0,80}}};
    c.physical_to_screen_={{3,0,1,2}};
    check(mark::orderScreenCorners(q,c).screen_order_->physical_to_screen_==c.physical_to_screen_,"installation mapping ignored");
    c.physical_to_screen_={{0,0,2,3}};
    check(!mark::orderScreenCorners(q,c).screen_order_,"invalid mapping accepted");
    mark::DetectorConfig d;d.corner_=c;bool rejected=false;
    try{mark::validateDetectorConfig(d);}catch(const std::exception&){rejected=true;}
    check(rejected,"config validation accepted duplicate mapping");
    c={};q[1]=q[0];check(!mark::orderScreenCorners(q,c).screen_order_,"duplicate point accepted");
    q={{{0,0},{1,0},{2,0},{3,0}}};check(!mark::orderScreenCorners(q,c).screen_order_,"collinear accepted");
    std::cout<<"fixed screen mapping and shake PASS\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
