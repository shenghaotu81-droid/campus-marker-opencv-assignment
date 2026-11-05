// 路线统计留在测试侧；可显式输入录像复跑，不给生产拟合器增加全局计数器。
#include "detector/config.hpp"
#include "model/marker_geometry_loader.hpp"
#include "preprocess/preprocess.hpp"
#include "geometry/observation/geometry_observation.hpp"
#include "geometry/parent/geometry_matcher.hpp"
#include "geometry/validation/geometry_validation.hpp"
#include "geometry/completion/geometry_assignment_completion.hpp"
#include "corners/observation/corner_observation.hpp"
#include "corners/fitting/corner_edge_fit.hpp"
#include "vision/math/observed_geometry.hpp"
#include "block3_fixture.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <opencv2/videoio.hpp>
namespace {
std::map<size_t,size_t> hits;
size_t failed=0;
void count(const mark::PreparedFrame &frame, const std::vector<mark::WhiteComponent> &components,
    const mark::GeometryHypothesis &h, const mark::MarkerGeometry &model,
    const mark::CornerConfig &config, size_t hypothesis, std::ostream &out) {
    mark::OriginalContourIndex index(frame, frame.frame_id_, *config.observation_budget_);
    for(size_t physical=0;physical<4;++physical) {
        const auto &binding=mark::model_corner_contract[physical];
        const mark::WhiteComponent *component=nullptr;
        for(const auto &a:h.assignments_) if(a.model_part_id_==binding.part)
            component=mark::findComponentById(components,a.component_id_);
        if(!component) return;
        const mark::GeometryPolygon *polygon=nullptr;
        for(const auto &p:model.polygons) if(p.id==binding.part) polygon=&p;
        if(!polygon) return;
        std::array<std::array<cv::Point2d,2>,2> edges;
        int edge_ids[]{binding.a,binding.b};
        for(int edge=0;edge<2;++edge) for(int end=0;end<2;++end)
            edges[edge][end]=frame.workToOriginal(mark::observed::project(h.affine_transform_,
                polygon->vertices[(edge_ids[edge]+end)%polygon->vertices.size()]));
        const auto &contour=index.observe(*component);
        if(!contour.reason.empty()) return;
        mark::EdgePairFitTrace trace;
        const auto fitted=mark::fitObservedEdgePair(contour.contour,edges,config,&trace);
        out<<frame.frame_id_<<'\t'<<hypothesis<<'\t'<<physical<<'\t';
        if(!fitted.evidence) { ++failed;out<<"FAIL\n";return; }
        const auto &e=*fitted.evidence;
        const auto route=trace.pairs.back().stage+1;
        ++hits[route];
        out<<route<<'\t'<<std::setprecision(17)<<e.intersection_.x<<'\t'<<e.intersection_.y
            <<'\t'<<e.line_mean_residual_px_[0]<<'\t'<<e.line_mean_residual_px_[1]
            <<'\t'<<e.corner_error_px_<<'\t'<<e.support_extension_px_[0]<<'\t'<<e.support_extension_px_[1]<<'\n';
    }
}
}
int main(int argc,char **argv) {
    try {
        const auto app=mark::loadConfig(std::filesystem::path(VISION_SOURCE_ROOT)/"config/detector.yaml");
        std::ofstream file;
        if(argc>2) { file.open(argv[2]); if(!file) throw std::runtime_error("output open failed"); }
        std::ostringstream fixture_output;
        std::ostream &out=argc>2 ? static_cast<std::ostream&>(file) : fixture_output;
        size_t frames=0;
        if(argc==1) {
            const auto s=fixture::scene();
            count(s.frame,s.frame.components_,s.hypothesis,s.geometry,fixture::cornerConfig(),0,out);
            frames=1;
            if(hits.empty())throw std::runtime_error("fixture produced no fitted corners");
        } else {
            cv::VideoCapture capture(argv[1]);
            if(!capture.isOpened())throw std::runtime_error("video open failed");
            const auto model=mark::loadMarkerGeometry(app.detector_config.marker_geometry_path_);
            cv::Mat image;
            while(capture.read(image)) {
                const auto &c=app.detector_config;
                auto frame=mark::preprocess({image,frames,int64_t(frames*14000),mark::TimestampSource::Unknown},c.preprocess);
                const auto components=mark::extractWhiteComponents(frame,c.geometry_);
                const auto observed=mark::observeShapes(components,c.geometry_);
                auto batch=mark::validateGeometryBatch(mark::generateGeometryHypotheses(observed,model,c.geometry_),model,components,c.geometry_);
                batch=mark::completeSegmentedAssignments(batch,components,model,*c.assignment_completion_);
                for(size_t h=0;h<batch.hypotheses_.size();++h)
                    if(batch.hypotheses_[h].completeness_!=mark::GeometryCompleteness::CLEARLY_INCOMPLETE)
                        count(frame,components,batch.hypotheses_[h],model,c.corner_,h,out);
                ++frames;
            }
        }
        if(!frames) throw std::runtime_error("video contains no frames");
        std::cout<<"frames="<<frames<<" failed="<<failed<<'\n';
        for(const auto &[route,n]:hits)std::cout<<"route "<<route<<" hits="<<n<<'\n';
        return 0;
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
