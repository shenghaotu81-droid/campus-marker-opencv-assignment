#include "detector/config.hpp"
#include "pipeline/decode/decode_stage.hpp"
#include <opencv2/videoio.hpp>
#include <fstream>
#include <iomanip>
int main(int argc,char**argv){auto c=mark::loadConfig(argv[1]);cv::VideoCapture v(argv[2]);std::ofstream out(argv[3]);out<<std::setprecision(17);cv::Mat im;uint64_t i=0;double fps=v.get(cv::CAP_PROP_FPS);while(v.read(im)){auto r=mark::runDecodePipeline({im,i,std::llround(i*1e6/fps),mark::TimestampSource::Unknown},c.detector_config);out<<i<<' '<<int(r.status)<<' '<<r.detections.size();for(auto &d:r.detections){for(auto p:d.corners)out<<' '<<p.x<<' '<<p.y;out<<' '<<d.quality_flags;for(int k:*d.attributes.orientation)out<<' '<<k;}out<<'\n';++i;}return i==1676?0:1;}
