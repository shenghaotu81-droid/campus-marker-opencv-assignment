#include "detector/config.hpp"
#include "preprocess/preprocess.hpp"
#include "geometry/observation/geometry_observation.hpp"
#include "geometry/matching/match_quad.hpp"
#include "corners/observation/corner_observation.hpp"
#include "vision/types/detection/corner_types.hpp"
#include "vision/pose/pose_estimator.hpp"
#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>
#include <fstream>
#include <iostream>
#include <iomanip>
std::optional<cv::Point2d> fit(const std::vector<cv::Point>& contour,cv::Point2d q, double eps,double trim,std::array<cv::Point2d,2> ends) {
    if(contour.size()<8)return {};
    size_t anchor=0;for(size_t i=1;i<contour.size();++i)if(cv::norm(cv::Point2d(contour[i])-q)<cv::norm(cv::Point2d(contour[anchor])-q))anchor=i;
    std::array<cv::Point2d,2> dir{ends[0]-q,ends[1]-q};for(auto &v:dir)v*=1/cv::norm(v);
    auto point=[&](int k){return cv::Point2d(contour[(k%int(contour.size())+contour.size())%contour.size()]);};
    cv::Vec4d lines[2];int previous=-1;
    for(int edge=0;edge<2;++edge){int sign=edge?1:-1;auto bounds=cv::boundingRect(contour);int probe=std::max(3,std::max(bounds.width,bounds.height)/4);auto motion=point(anchor+sign*probe)-point(anchor);int group=motion.dot(dir[1])>motion.dot(dir[0]);if(group==previous){std::cerr<<"samegroup q="<<q<<" anchor="<<point(anchor)<<" motion="<<motion<<"\n";return {};}previous=group;
        std::vector<cv::Point2d> support;bool started=false;double walk=0;auto last=point(anchor);
        for(size_t step=1;step<contour.size()/2;++step){int k=int(anchor)+sign*step;auto p=point(k);walk+=cv::norm(p-last);last=p;if(walk<trim+eps)continue;
            auto tangent=point(k+int(eps))-point(k-int(eps));int label=std::abs(tangent.dot(dir[1]))>std::abs(tangent.dot(dir[0]));
            if(label!=group){if(started)break;continue;}started=true;support.push_back(p);
        }
        double tail=0;while(support.size()>1 && tail<trim){tail+=cv::norm(support.back()-support[support.size()-2]);support.pop_back();}
        if(support.size()<2){std::cerr<<"short q="<<q<<" group="<<group<<" n="<<support.size()<<"\n";return {};}cv::fitLine(support,lines[edge],cv::DIST_L2,0,.01,.01);
    }
    cv::Point2d a(lines[0][2],lines[0][3]),b(lines[1][2],lines[1][3]),v(lines[0][0],lines[0][1]),w(lines[1][0],lines[1][1]);double cross=v.cross(w);if(std::abs(cross)<1e-12)return {};auto p=a+v*((b-a).cross(w)/cross);if(!std::isfinite(p.x)||!std::isfinite(p.y))return {};return p;
}
int main(int argc,char**argv){auto cfg=mark::loadConfig("/tmp/single-original-config/detector.yaml").detector_config;auto pc=mark::pose::load_pose_config("/tmp/single-original-config/pose.yaml");mark::pose::PoseEstimator solver(pc);cv::VideoCapture video("data/raw/marker_video.avi");std::ofstream out(argv[1]);out<<std::setprecision(17);double eps=std::stod(argv[2]),trim=std::stod(argv[3]);cv::Mat im;int frame=0,ok=0,fail=0;while(video.read(im)){auto f=mark::preprocess({im,uint64_t(frame),frame*14000,mark::TimestampSource::Unknown},cfg.preprocess);auto matches=mark::matchQuads(f,mark::extractWhiteComponents(f,cfg.geometry_),cfg.geometry_);mark::OriginalContourIndex index(f);
 for(auto &q:matches){auto s=mark::orderScreenCorners(q,cfg.corner_);if(!s.screen_order_)continue;std::array<cv::Point2d,4> corners;bool valid=true;for(int i=0;i<4;++i){auto c=index.atCorner(s.screen_order_->screen_points_[i]);auto p=fit(c.contour,s.screen_order_->screen_points_[i],eps,trim,{s.screen_order_->screen_points_[(i+3)%4],s.screen_order_->screen_points_[(i+1)%4]});if(!p){valid=false;break;}corners[i]=cv::Point2f(*p);}
 if(!valid){++fail;out<<frame<<" FAIL\n";continue;}++ok;mark::pose::CaptureStamp stamp;stamp.session_id_="probe";stamp.camera_id_=pc.camera_.camera_id_;stamp.clock_domain_="nominal";stamp.pixel_domain_=mark::pose::PixelDomain::RawDistorted;stamp.frame_id_=frame;stamp.received_us_=frame*14000;auto pose=solver.estimate_planar(pc.mark_points_m_,corners,im.size(),stamp,pc.mark_target_id_);double best=INFINITY;for(auto &c:pose.candidates_)if(c.metrics_valid_)best=std::min(best,c.rmse_px_);out<<frame<<' '<<best;for(auto p:corners)out<<' '<<p.x<<' '<<p.y;out<<'\n';}
 ++frame;}std::cout<<"frames="<<frame<<" ok="<<ok<<" fail="<<fail<<'\n';}
