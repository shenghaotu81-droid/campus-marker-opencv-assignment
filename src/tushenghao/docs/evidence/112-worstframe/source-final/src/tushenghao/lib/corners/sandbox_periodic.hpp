// 沙盒C1/C2：显式区分物理直线和扫描行周期项，保留全部原像素来源，不提高任何质量阈值。
#pragma once
#include <opencv2/core.hpp>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <set>
#include <map>
namespace mark::sandbox {
inline int modelMode(){const char*v=std::getenv("MARK_SANDBOX_MODEL");return v&&std::string(v)=="c1"?1:v&&std::string(v)=="c1median"?3:v&&std::string(v)=="c2"?2:0;}
inline thread_local double frame_shift=0;
struct PeriodicFit{cv::Vec4d line{};int axis=1;double coefficient=0;bool valid=false;};
inline double phase(cv::Point2d p,int axis){// 原始整数像素走无libm调用的精确路径；非整数/越界输入仍沿用原llround语义。
 double coordinate=axis?p.y:p.x;if(coordinate>=std::numeric_limits<int>::min()&&coordinate<=std::numeric_limits<int>::max()){int integer=static_cast<int>(coordinate);if(coordinate==integer)return (integer&1)?1.:-1.;}return (static_cast<long long>(std::llround(coordinate))&1)?1.:-1.;}
// 中心化闭式OLS，不为每个弧构造矩阵/调用通用求解器。
inline PeriodicFit fitPeriodic(const std::vector<cv::Point2d>&points){
 PeriodicFit result;if(points.size()<3)return result;double minx=INFINITY,maxx=-INFINITY,miny=INFINITY,maxy=-INFINITY;
 for(auto p:points){minx=std::min(minx,p.x);maxx=std::max(maxx,p.x);miny=std::min(miny,p.y);maxy=std::max(maxy,p.y);}result.axis=maxy-miny>=maxx-minx?1:0;
 double mt=0,mz=0,mh=0;for(auto p:points){mt+=result.axis?p.y:p.x;mz+=result.axis?p.x:p.y;mh+=phase(p,result.axis);}double n=points.size();mt/=n;mz/=n;mh/=n;
 double tt=0,hh=0,th=0,tz=0,hz=0;for(auto p:points){double t=(result.axis?p.y:p.x)-mt,z=(result.axis?p.x:p.y)-mz,h=phase(p,result.axis)-mh;tt+=t*t;hh+=h*h;th+=t*h;tz+=t*z;hz+=h*z;}
 double determinant=tt*hh-th*th;if(!(determinant>64*std::numeric_limits<double>::epsilon()*std::max(1.,tt*hh)))return result;
 double a=(tz*hh-hz*th)/determinant,c=(hz*tt-tz*th)/determinant,b=mz-c*mh;double norm=std::hypot(a,1.);
 result.line=result.axis?cv::Vec4d(a/norm,1/norm,b,mt):cv::Vec4d(1/norm,a/norm,mt,b);result.coefficient=c;result.valid=true;return result;
}
// 门控是平均绝对垂距；固定OLS斜率后，以两相位中位截距最小化该目标，模型仍只有a/b/c。
inline PeriodicFit fitPeriodicMedian(const std::vector<cv::Point2d>&points){
 auto result=fitPeriodic(points);if(!result.valid)return result;
 double slope=result.axis?result.line[0]/result.line[1]:result.line[1]/result.line[0];
 double origin=result.axis?result.line[3]:result.line[2];std::vector<double> groups[2];
 for(auto p:points){double t=result.axis?p.y:p.x,z=result.axis?p.x:p.y;groups[phase(p,result.axis)>0?1:0].push_back(z-slope*(t-origin));}
 double medians[2];for(int k=0;k<2;++k){auto&v=groups[k];if(v.empty()){result.valid=false;return result;}auto mid=v.begin()+v.size()/2;std::nth_element(v.begin(),mid,v.end());medians[k]=*mid;if(v.size()%2==0)medians[k]=.5*(medians[k]+*std::max_element(v.begin(),mid));}
 double b=.5*(medians[0]+medians[1]);result.coefficient=.5*(medians[1]-medians[0]);double norm=std::hypot(slope,1.);
 result.line=result.axis?cv::Vec4d(slope/norm,1/norm,b,origin):cv::Vec4d(1/norm,slope/norm,origin,b);return result;
}
// 最慢帧实验：零斜率是同一周期模型的通用候选，不绑定帧号；保留全部像素和原质量门限。
inline PeriodicFit fitPeriodicZero(const std::vector<cv::Point2d>&points){
 auto result=fitPeriodic(points);if(!result.valid)return result;std::vector<double>groups[2];
 for(auto p:points)groups[phase(p,result.axis)>0?1:0].push_back(result.axis?p.x:p.y);
 double m[2];for(int k=0;k<2;++k){auto&v=groups[k];auto mid=v.begin()+v.size()/2;std::nth_element(v.begin(),mid,v.end());m[k]=*mid;if(v.size()%2==0)m[k]=.5*(m[k]+*std::max_element(v.begin(),mid));}
 double b=.5*(m[0]+m[1]);result.coefficient=.5*(m[1]-m[0]);result.line=result.axis?cv::Vec4d(0,1,b,result.line[3]):cv::Vec4d(1,0,result.line[2],b);return result;
}
// 对同一a/b/c模型精化斜率：消去两相位中位截距后，坐标绝对残差是凸函数。
inline PeriodicFit fitPeriodicLAD(const std::vector<cv::Point2d>&points){
 auto initial=fitPeriodicMedian(points);if(!initial.valid)return initial;
 int axis=initial.axis;double origin=axis?initial.line[3]:initial.line[2];double minz=INFINITY,maxz=-INFINITY;
 for(auto p:points){double z=axis?p.x:p.y;minz=std::min(minz,z);maxz=std::max(maxz,z);}
 // 沙盒性能优化：凸搜索复用两相位缓存，避免每次目标求值重新分配内存。
 std::vector<double> groups[2];groups[0].reserve(points.size());groups[1].reserve(points.size());
 auto evaluate=[&](double slope){PeriodicFit result=initial;groups[0].clear();groups[1].clear();
  for(auto p:points){double t=axis?p.y:p.x,z=axis?p.x:p.y;groups[phase(p,axis)>0?1:0].push_back(z-slope*(t-origin));}
  double medians[2];for(int k=0;k<2;++k){auto&v=groups[k];auto mid=v.begin()+v.size()/2;std::nth_element(v.begin(),mid,v.end());medians[k]=*mid;if(v.size()%2==0)medians[k]=.5*(medians[k]+*std::max_element(v.begin(),mid));}
  double b=.5*(medians[0]+medians[1]);result.coefficient=.5*(medians[1]-medians[0]);double norm=std::hypot(slope,1.);
  result.line=axis?cv::Vec4d(slope/norm,1/norm,b,origin):cv::Vec4d(1/norm,slope/norm,origin,b);
  double objective=0;for(auto p:points){double t=axis?p.y:p.x,z=axis?p.x:p.y;objective+=std::abs(z-slope*(t-origin)-b-result.coefficient*phase(p,axis));}return std::make_pair(objective,result);
 };
 // 每个扫描坐标的L1中位损失之和，是任何a/b/c拟合的全局下界。
 // 零斜率恰好达到此下界时可直接返回，比较严格相等，不放宽残差门限。
 std::map<double,std::vector<double>> scan_groups;
 for(auto p:points)scan_groups[axis?p.y:p.x].push_back(axis?p.x:p.y);
 double lower_bound=0;for(auto &entry:scan_groups){auto &values=entry.second;auto middle=values.begin()+values.size()/2;std::nth_element(values.begin(),middle,values.end());double median=*middle;for(double z:values)lower_bound+=std::abs(z-median);}
 auto zero=evaluate(0);
 if(zero.first==lower_bound){double slope=axis?initial.line[0]/initial.line[1]:initial.line[1]/initial.line[0];auto original=evaluate(slope);return original.first<=zero.first?original.second:zero.second;}
 double bound=std::max(1.,maxz-minz),lo=-bound,hi=bound;
 for(int step=0;step<40;++step){double left=(2*lo+hi)/3,right=(lo+2*hi)/3;if(evaluate(left).first<=evaluate(right).first)hi=right;else lo=left;}
 double original_slope=axis?initial.line[0]/initial.line[1]:initial.line[1]/initial.line[0];auto best=evaluate(original_slope);
 auto consider=[&](double slope){auto v=evaluate(slope);if(v.first<best.first)best=std::move(v);};consider((lo+hi)/2);consider(0);
 // 把凸搜索区间中的整数像素斜率拐点显式复算，避免浮点近阈值误判。
 std::set<double> seen_knots;std::vector<double> knots;
 for(size_t i=0;i<points.size();++i)for(size_t j=0;j<i;++j){if(phase(points[i],axis)!=phase(points[j],axis))continue;double dt=(axis?points[i].y:points[i].x)-(axis?points[j].y:points[j].x);if(dt==0)continue;double slope=((axis?points[i].x:points[i].y)-(axis?points[j].x:points[j].y))/dt;if(slope>=lo&&slope<=hi&&seen_knots.insert(slope).second)knots.push_back(slope);}
 // 相同斜率拐点只复算一次；不改变候选集合及目标函数。
 for(double slope:knots)consider(slope);
 return best.second;
}
inline double correctedDistance(cv::Point2d p,const cv::Vec4d&line,int kind,int axis,double coefficient){
 double signed_distance=line[0]*(p.y-line[3])-line[1]*(p.x-line[2]);
 if(kind==1||kind==3||kind==4||kind==5){double scale=axis?-line[1]:line[0];signed_distance-=scale*coefficient*phase(p,axis);}
 else if(kind==2)signed_distance+=line[1]*coefficient*phase(p,1);
 return std::abs(signed_distance);
}
// C2仅每帧估计一个水平奇偶行位移；模型不能按当前失败角单独调参。
inline double estimateFrameShift(const std::vector<std::vector<cv::Point>>&contours){
 std::vector<double> coefficients;
 for(auto &ring:contours){if(ring.size()<48)continue;for(size_t start=0;start+48<=ring.size();start+=24){std::vector<cv::Point2d>window;for(size_t k=start;k<start+48;++k)window.push_back(ring[k]);auto f=fitPeriodic(window);if(!f.valid||f.axis!=1||std::abs(f.line[0])>std::sin(3*CV_PI/180))continue;double lo=INFINITY,hi=-INFINITY;for(auto p:window){lo=std::min(lo,p.y);hi=std::max(hi,p.y);}if(hi-lo<14)continue;coefficients.push_back(f.coefficient);}}
 if(coefficients.empty())return 0;auto middle=coefficients.begin()+coefficients.size()/2;std::nth_element(coefficients.begin(),middle,coefficients.end());return *middle;
}
}
