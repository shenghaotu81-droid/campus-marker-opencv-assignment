
#include "vision/synth_ref/transforms.hpp"
#include <cmath>
namespace sg  {
  // 已知齐次变换检查有限分母，避免退化 fixture 被错误求逆。
  Point transform_point(const cv::Matx33d& m,Point p)  {
    cv::Vec3d v=m*cv::Vec3d(p.x,p.y,1);
    if(!std::isfinite(v[2])||std::abs(v[2])<1e-12)throw Error(2,"变换分母非有限或为零");
    Point q(v[0]/v[2],v[1]/v[2]);
    if(!std::isfinite(q.x)||!std::isfinite(q.y))throw Error(2,"变换结果非有限");
    return q;
  }
  // 保持输入顶点的物理编号，所有操作在连续坐标完成。
  Polygon transform_polygon(const cv::Matx33d& m,const Polygon& p)  {
    Polygon q;
    for(auto v:p)q.push_back(transform_point(m,v));
    return q;
  }
  // 用逆转置法向算两笔画宽，不能将 M 的缺口当笔画。
  std::array<double,2> stroke_widths(const cv::Matx33d& m,double w)  {
    double a=m(0,0),b=m(0,1),c=m(1,0),d=m(1,1),det=a*d-b*c;
    if(!std::isfinite(det)||std::abs(det)<1e-12)throw Error(2,"奇异矩阵不能求笔画宽/逆矩阵");
    return  {
      w*std::abs(det)/std::hypot(d,b),w*std::abs(det)/std::hypot(c,a)
    };
  }
  // 闭式求二维奇异值比，是矩阵测量而非剪切参数 h。
  double singular_ratio(const cv::Matx33d& m)  {
    double a=m(0,0),b=m(0,1),c=m(1,0),d=m(1,1),tr=a*a+b*b+c*c+d*d,det=a*d-b*c;
    double hi=(tr+std::sqrt(std::max(0.0,tr*tr-4*det*det)))/2;
    if(hi<=0||std::abs(det)<1e-12)throw Error(2,"奇异矩阵无有限奇异值比");
    return hi/std::abs(det);
  }
  // 按 x 右 y 下正角顺时针定义 R，按模型外框中心平移。
  static cv::Matx33d affine_values(double model_stroke,Point center,const Json& d)  {
    double theta=d.value("theta",0.0)*std::acos(-1.0)/180,r=d.value("ratio",1.0),h=d.value("shear",0.0),w=d.value("width",8.0);
    double co=std::cos(theta),si=std::sin(theta);
    cv::Matx33d a(co*r,co*h-si,0,si*r,si*h+co,0,0,0,1);
    auto widths=stroke_widths(a,model_stroke);
    double s=w/std::min(widths[0],widths[1]);
    for(int y=0;y<2;++y)for(int x=0;x<2;++x)a(y,x)*=s;
    double px=0,py=0;
    if(d.contains("phase"))  {
      px=d["phase"][0];
      py=d["phase"][1];
    }
    a(0,2)=480+px-a(0,0)*center.x-a(0,1)*center.y;
    a(1,2)=360+py-a(1,0)*center.x-a(1,1)*center.y;
    return a;
  }
  cv::Matx33d affine(const Context& c,const Json& d) {
    return affine_values(c.model.at("stroke_width").at("value"),c.center,d);
  }

}
