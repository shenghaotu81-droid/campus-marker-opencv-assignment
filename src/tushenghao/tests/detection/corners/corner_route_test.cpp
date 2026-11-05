// 路线统计与冻结输入重放仅在测试侧；生产没有全局计数器或旧对应规则。
#include "detector/config.hpp"
#include "preprocess/preprocess.hpp"
#include "geometry/observation/geometry_observation.hpp"
#include "corners/observation/corner_observation.hpp"
#include "corners/fitting/corner_edge_fit.hpp"
#include "block3_fixture.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <opencv2/videoio.hpp>
namespace {
std::map<size_t,size_t> hits;
size_t failed=0,calls=0;
using Edges=std::array<std::array<cv::Point2d,2>,2>;
bool record(uint64_t frame,size_t match,size_t corner,const std::vector<cv::Point> &contour,
            const Edges &edges,const mark::CornerConfig &config,std::ostream &out) {
    ++calls;
    mark::EdgePairFitTrace trace;
    const auto fit=mark::fitObservedEdgePair(contour,edges,config,&trace);
    out<<frame<<'\t'<<match<<'\t'<<corner<<'\t';
    if(!fit.evidence){++failed;out<<"FAIL\n";return false;}
    const auto &e=*fit.evidence;
    const auto route=trace.pairs.back().stage+1;
    ++hits[route];
    out<<route<<'\t'<<std::setprecision(17)<<e.intersection_.x<<'\t'<<e.intersection_.y
       <<'\t'<<e.line_mean_residual_px_[0]<<'\t'<<e.line_mean_residual_px_[1]
       <<'\t'<<e.corner_error_px_<<'\t'<<e.support_extension_px_[0]<<'\t'<<e.support_extension_px_[1]<<'\n';
    return true;
}
void count(const mark::PreparedFrame &frame,const mark::MatchQuad &q,
           const mark::CornerConfig &config,size_t match,std::ostream &out) {
    mark::OriginalContourIndex index(frame);
    for(size_t i=0;i<4;++i) {
        const Edges edges{{{q[(i+3)%4],q[i]},{q[i],q[(i+1)%4]}}};
        const auto original=index.atCorner(q[i]);
        if(!original.reason.empty()){++failed;return;}
        if(!record(frame.frame_id_,match,i,original.contour,edges,config,out))return;
    }
}
size_t frozen(const char *path,const mark::CornerConfig &config,std::ostream &out) {
    std::ifstream input(path);
    if(!input)throw std::runtime_error("frozen input open failed");
    std::set<uint64_t> frames;
    for(;;) {
        uint64_t frame;size_t match,corner,n;
        if(!(input>>frame)) {
            if(input.eof())break;
            throw std::runtime_error("invalid frozen frame header");
        }
        if(!(input>>match>>corner>>n) || corner>=4 || n<3)
            throw std::runtime_error("invalid frozen fit header");
        Edges edges;
        for(auto &edge:edges)for(auto &p:edge)input>>p.x>>p.y;
        std::vector<cv::Point> contour(n);
        for(auto &p:contour)input>>p.x>>p.y;
        if(!input)throw std::runtime_error("truncated frozen fit input");
        record(frame,match,corner,contour,edges,config,out);
        frames.insert(frame);
    }
    return frames.size();
}
}
int main(int argc,char **argv) {
    try {
        const bool replay=argc==4 && std::string(argv[1])=="--frozen";
        if((argc>3 && !replay) || (argc>1 && std::string(argv[1])=="--frozen" && !replay))
            throw std::runtime_error("usage: corner_route_test [VIDEO [OUT.tsv]] | --frozen INPUT.txt OUT.tsv");
        const auto app=mark::loadConfig(std::filesystem::path(VISION_SOURCE_ROOT)/"config/detector.yaml");
        std::ofstream file;
        if(argc>2){file.open(argv[replay?3:2]);if(!file)throw std::runtime_error("output open failed");}
        std::ostringstream buffer;
        std::ostream &out=argc>2?static_cast<std::ostream&>(file):buffer;
        size_t frames=0;
        if(replay)frames=frozen(argv[2],app.detector_config.corner_,out);
        else if(argc==1) {
            const auto s=fixture::scene();count(s.frame,s.quad,fixture::cornerConfig(),0,out);frames=1;
            if(hits.empty())throw std::runtime_error("fixture produced no fitted corners");
        } else {
            cv::VideoCapture capture(argv[1]);
            if(!capture.isOpened())throw std::runtime_error("video open failed");
            cv::Mat image;
            while(capture.read(image)) {
                const auto &c=app.detector_config;
                const auto frame=mark::preprocess({image,frames,int64_t(frames*14000),mark::TimestampSource::Unknown},c.preprocess);
                const auto components=mark::extractWhiteComponents(frame,c.geometry_);
                const auto matches=mark::matchQuads(frame,components,c.geometry_);
                for(size_t i=0;i<matches.size();++i)count(frame,matches[i],c.corner_,i,out);
                ++frames;
            }
        }
        if(!frames)throw std::runtime_error("input contains no frames");
        std::cout<<"frames="<<frames<<" calls="<<calls<<" failed="<<failed<<'\n';
        for(const auto &[route,n]:hits)std::cout<<"route "<<route<<" hits="<<n<<'\n';
        return failed?1:0;
    } catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
