// 中文用途：冻结角输入重放最终模型，输出所选完整支持、物理线、周期系数以及原始/校正两类残差。
#include "config/config.hpp"
#include "corners/corner_edge_fit.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
int main(){cv::setNumThreads(1);auto cfg=mark::loadConfig("src/tushenghao/config/detector_verification.yaml").detector_config.corner_;std::ifstream f("docs/evidence/112-forensics/deep/corner_inputs.txt");std::string line;std::cout<<std::setprecision(17);while(std::getline(f,line)){std::istringstream in(line);int fid,h,p,cid,n;in>>fid>>h>>p>>cid>>n;if(fid!=153&&fid!=382&&fid!=784)continue;std::array<std::array<cv::Point2d,2>,2>edges;for(auto&e:edges)for(auto&q:e)in>>q.x>>q.y;std::vector<cv::Point>ring(n);for(auto&q:ring)in>>q.x>>q.y;auto r=mark::fitObservedEdgePair(ring,edges,cfg);std::cout<<"{\"frame\":"<<fid<<",\"hypothesis\":"<<h<<",\"physical\":"<<p<<",\"success\":"<<bool(r.evidence);
 if(r.evidence){auto&e=*r.evidence;std::cout<<",\"corner\":["<<e.intersection_.x<<","<<e.intersection_.y<<"],\"edges\":[";for(int k=0;k<2;++k){if(k)std::cout<<",";auto l=k?e.line_b_:e.line_a_;std::cout<<"{\"kind\":"<<e.sandbox_model_kind_[k]<<",\"axis\":"<<e.sandbox_model_axis_[k]<<",\"coefficient\":"<<e.sandbox_model_coefficient_[k]<<",\"raw_mean\":"<<e.line_mean_residual_px_[k]<<",\"model_mean\":"<<e.sandbox_model_mean_[k]<<",\"line\":["<<l[0]<<","<<l[1]<<","<<l[2]<<","<<l[3]<<"],\"support\":[";bool first=true;for(auto q:e.original_support_arcs_[k]){if(!first)std::cout<<",";first=false;std::cout<<"["<<q.x<<","<<q.y<<"]";}std::cout<<"]}";}std::cout<<"]";}std::cout<<",\"reason\":"<<std::quoted(r.reason)<<"}\n";}}
