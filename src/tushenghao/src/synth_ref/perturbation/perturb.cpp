
#include "vision/synth_ref/perturb.hpp"
#include <cmath>
namespace sg  {
  // 偏移支撑线而不是顶点位移；绕向决定外法向，L3 的反绕向也正确。
  bool offset_polygon(const Polygon& p,const std::vector<double>& d,Polygon& out)  {
    out.clear();
    if(!valid_polygon(p)||d.size()!=p.size())return false;
    double orientation=signed_area(p)>0?1:-1;
    std::vector<Point> normals;
    std::vector<double> constants;
    for(size_t i=0;i<p.size();++i)  {
      Point edge=p[(i+1)%p.size()]-p[i];
      double len=cv::norm(edge);
      Point n(orientation*edge.y/len,-orientation*edge.x/len);
      normals.push_back(n);
      constants.push_back(n.dot(p[i])+d[i]);
    }
    // 相邻线交点保持编号，不对坍缩形状做修补或删样。
    for(size_t i=0;i<p.size();++i)  {
      size_t j=(i+p.size()-1)%p.size();
      Point a=normals[j],b=normals[i];
      double det=a.cross(b);
      if(std::abs(det)<1e-12)  {
        out.clear();
        return false;
      }
      out.emplace_back((constants[j]*b.y-a.y*constants[i])/det,(a.x*constants[i]-constants[j]*b.x)/det);
    }
    // 收缩穿过骨架时边会反向；即使面积偶然为正也不应当作合法偏移。
    if(!valid_polygon(out)||signed_area(out)*signed_area(p)<=0)  {
      out.clear();
      return false;
    }
    for(size_t i=0;i<p.size();++i)if((out[(i+1)%p.size()]-out[i]).dot(p[(i+1)%p.size()]-p[i])<=1e-10)  {
      out.clear();
      return false;
    }
    return true;
  }
}
