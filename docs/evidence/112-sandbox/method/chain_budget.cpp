// 中文用途：独立理想矩形的奇偶行位移，直接计算完整链码外边，不做昂贵候选搜索；解释2px压力为何仍应严格拒绝。
#include "corners/sandbox_periodic.hpp"
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <map>
#include <iomanip>
int main(){cv::setNumThreads(1);std::cout<<std::setprecision(17);for(int amplitude=0;amplitude<=2;++amplitude){cv::Mat img=cv::Mat::zeros(64,64,CV_8UC1);for(int y=8;y<=55;++y)for(int x=20;x<=40;++x)img.at<unsigned char>(y,x+amplitude*(y%2?1:-1))=255;std::vector<std::vector<cv::Point>>contours;cv::findContours(img,contours,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_NONE);std::vector<cv::Point2d>support;std::map<int,std::vector<int>>rows;for(auto p:contours.at(0))if(p.x<30&&p.y>=12&&p.y<=50){support.push_back(p);rows[p.y].push_back(p.x);}double lower=0;for(auto &kv:rows){auto v=kv.second;std::sort(v.begin(),v.end());double median=v[v.size()/2];for(double z:v)lower+=std::abs(z-median);}auto fit=mark::sandbox::fitPeriodicLAD(support);double error=0;for(auto p:support)error+=mark::sandbox::correctedDistance(p,fit.line,4,fit.axis,fit.coefficient);std::cout<<"{\"amplitude\":"<<amplitude<<",\"support_count\":"<<support.size()<<",\"row_L1_lower_bound_mean\":"<<lower/support.size()<<",\"model_mean\":"<<error/support.size()<<",\"coefficient\":"<<fit.coefficient<<",\"line_x\":"<<fit.line[2]<<"}\n";}}
