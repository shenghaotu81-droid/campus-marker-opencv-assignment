// 中文用途：沙盒通过公共Detector运行完整视频，固定OpenCV线程数，记录raw结果与各阶段时间，避免把不同计时范围混为一谈。
#include "mark/detector.hpp"
#include "corners/sandbox_profile.hpp"
#include "config/config.hpp"
#include "pipeline/detector_diagnostics_access.hpp"
#include <opencv2/videoio.hpp>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <chrono>
int main(int argc,char**argv){cv::setNumThreads(std::stoi(argv[2]));auto app=mark::loadConfig(argv[1]);mark::Detector detector(app.detector_config);cv::VideoCapture cap("data/raw/marker_video.avi");double fps=cap.get(cv::CAP_PROP_FPS);cv::Mat img;int frame=0;std::cout<<std::setprecision(17);while(cap.read(img)){
 mark::FrameInput input{};input.image=img;input.frame_id=frame;input.timestamp_us=std::llround(frame*1e6/fps);input.time_source=mark::TimestampSource::Unknown;
 mark::DiagnosticsRequest req;req.timing_enabled=true;req.level=mark::DiagnosticsLevel::Summary;req.scope=mark::ExecutionScope::Full;req.run_id="sandbox";mark::DetectorDiagnosticsAccess::prepare(detector,req);
 mark::sandbox::reset();auto start=std::chrono::steady_clock::now();auto r=detector.process(input);auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();auto rec=mark::DetectorDiagnosticsAccess::take(detector);
 std::cout<<"{\"frame\":"<<frame<<",\"status\":"<<int(r.status)<<",\"elapsed_ms\":"<<elapsed<<",\"detections\":[";bool first=true;for(auto&d:r.detections){if(!first)std::cout<<",";first=false;std::cout<<"[";for(int k=0;k<4;++k){if(k)std::cout<<",";std::cout<<"["<<d.corners[k].x<<","<<d.corners[k].y<<"]";}std::cout<<"]";}
 std::cout<<"],\"timings\":{";first=true;for(auto stage:{mark::Stage::Preprocess,mark::Stage::Detect,mark::Stage::Decode,mark::Stage::Stabilize}){auto s=rec.timings[static_cast<size_t>(stage)];if(!first)std::cout<<",";first=false;std::cout<<"\""<<int(stage)<<"\":"<<std::chrono::duration<double,std::milli>(s.elapsed.value_or(std::chrono::nanoseconds{})).count();}std::cout<<"},\"functions\":{";first=true;for(int k=0;k<mark::sandbox::Count;++k){if(!first)std::cout<<",";first=false;std::cout<<"\""<<mark::sandbox::names[k]<<"\":{\"ns\":"<<mark::sandbox::samples[k].ns<<",\"calls\":"<<mark::sandbox::samples[k].calls<<"}";}std::cout<<"}}\n";++frame;
 }std::cerr<<"frames="<<frame<<" threads="<<cv::getNumThreads()<<" fps="<<fps<<"\n";}
