// 沙盒独立定位验收：不读取112帧视频，已知图纸真值生成全局/局部扫描行扰动，区分调试集和封存测试集。
#include "block3_fixture.hpp"
#include "config/config.hpp"
#include "corners/corner_resolver.hpp"
#include "corners/detection_validator.hpp"
#include "corners/sandbox_periodic.hpp"
#include "core/observed_geometry_utils.hpp"
#include <iostream>
#include <iomanip>
#include <random>
#include <cstdlib>
int main(int argc,char**argv){cv::setNumThreads(1);auto c=mark::loadConfig("src/tushenghao/config/detector_verification.yaml").detector_config.corner_;bool test=std::string(argv[1])=="test";std::mt19937 rng(test?7919:112);std::uniform_int_distribution<int> shifts(-15,15);std::uniform_real_distribution<double> angles(-6,6);int count=test?120:36;std::cout<<std::setprecision(17);
 for(int id=0;id<count;++id){double angle=angles(rng);int amplitude=id%3;int local=(id/3)%2;int dx=shifts(rng),dy=shifts(rng);
  // 收敛预算：保留全部0/1px测试，2px宽链码压力每个独立分区只保留固定id=2；选择在测试分区首次执行前冻结。
  // 负样本在已覆盖的0/1px观测域内测试缺失支持，避免先被超预算噪声拒绝而掩盖缺角判据。
  if(test&&id>=100)amplitude=(id-100)%2;
  if(amplitude==2&&id!=2)continue;
  auto s=fixture::scene(1440,1080,angle);
  // 与视频无关：物理目标位置由图纸变换给出，扰动的奇偶平均为零。
  s.hypothesis.affine_transform_.at<double>(0,2)+=dx;s.hypothesis.affine_transform_.at<double>(1,2)+=dy;
  cv::Mat translated;cv::Mat transform=(cv::Mat_<double>(2,3)<<1,0,dx,0,1,dy);cv::warpAffine(s.frame.original_image_,translated,transform,s.frame.original_image_.size(),cv::INTER_NEAREST,cv::BORDER_CONSTANT);
  for(auto&comp:s.frame.components_)for(auto&p:comp.contour_){p.x+=dx;p.y+=dy;}
  s.frame.original_image_=translated.clone();double center=1000+dx;
  if(amplitude){s.frame.original_image_.setTo(cv::Scalar(0,0,0));for(int y=0;y<translated.rows;++y){int phase=y%2?1:-1;for(int x=0;x<translated.cols;++x){int sign=local?(x<center?-1:1):1;int target=x+amplitude*phase*sign;if(target>=0&&target<translated.cols)s.frame.original_image_.at<cv::Vec3b>(y,target)=translated.at<cv::Vec3b>(y,x);}}}
  std::array<cv::Point2d,4>truth;const char*parts[]={"L0","M1","L2","L3"};for(int k=0;k<4;++k)for(auto&part:s.geometry.polygons)if(part.id==parts[k])truth[k]=s.frame.workToOriginal(mark::observed::project(s.hypothesis.affine_transform_,part.vertices[k==1?1:0]));
  int negative=test&&id>=100?1+(id-100)%2:0;
  if(negative==1)s.frame.original_image_.setTo(cv::Scalar(0,0,0));
  if(negative==2)cv::circle(s.frame.original_image_,cv::Point(truth[1]),15,cv::Scalar(0,0,0),-1);
  auto r=mark::resolveObservedCorners(s.frame,s.hypothesis,s.geometry,c);bool valid=false;double error=-1;int modeled=0;bool tamper_rejected=true;
  if(r.measurement_){auto order=mark::orderScreenCorners(r.measurement_->physical_corners_,c);if(order.screen_order_)valid=mark::validateDetectionGeometry(*r.measurement_,*order.screen_order_,s.frame.original_image_.size(),c).valid_;error=0;for(int k=0;k<4;++k){error=std::max(error,cv::norm(r.measurement_->physical_corners_[k]-truth[k]));for(int e=0;e<2;++e)modeled+=r.measurement_->evidence_[k].sandbox_model_kind_[e]!=0;}
   if(valid&&modeled){auto changed=*r.measurement_;bool altered=false;for(auto&e:changed.evidence_)for(int a=0;a<2;++a)if(!altered&&e.sandbox_model_kind_[a]){e.sandbox_model_coefficient_[a]+=.25;altered=true;}auto o=mark::orderScreenCorners(changed.physical_corners_,c);tamper_rejected=!mark::validateDetectionGeometry(changed,*o.screen_order_,s.frame.original_image_.size(),c).valid_;}
  }
  std::cout<<"{\"id\":"<<id<<",\"split\":\""<<(test?"test":"calibration")<<"\",\"amplitude\":"<<amplitude<<",\"local\":"<<local<<",\"angle\":"<<angle<<",\"negative\":"<<negative<<",\"measurement\":"<<bool(r.measurement_)<<",\"valid\":"<<valid<<",\"max_truth_error\":"<<error<<",\"modeled_edges\":"<<modeled<<",\"tamper_rejected\":"<<tamper_rejected<<",\"reason\":"<<std::quoted(r.rejection_reason_)<<"}\n";
 }
}
