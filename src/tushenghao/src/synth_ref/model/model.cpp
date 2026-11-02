#include "vision/synth_ref/transforms.hpp"

#include "vision/synth_ref/model.hpp"
#include <fstream>
#include <regex>
#include <set>
#include <cmath>
#include <sstream>
namespace sg {
  // 把 JSON 坐标转为连续 double，不提前取整。
  Polygon polygon(const Json& j)  {
    Polygon p;
    for(const auto& v:j)  {
      if(!v.is_array()||v.size()!=2) throw Error(2,"vertices 必须是二维坐标");
      double x=v[0],y=v[1];
      if(!std::isfinite(x)||!std::isfinite(y)) throw Error(2,"非有限坐标");
      p.emplace_back(x,y);
    }
    return p;
  }
  // 鞋带公式按顶点计算面积，M 不使用预写面积值。
  double signed_area(const Polygon& p)  {
    double a=0;
    for(size_t i=0;i<p.size();++i) a+=p[i].cross(p[(i+1)%p.size()]);
    return a/2;
  }
  // 叉积用于连续几何的方向和线段相交判断。
  static double cross(Point a,Point b,Point c)  {
    return (b-a).cross(c-a);
  }
  // 只用数值舍入界识别边上点，不作为检测器误差门限。
  static bool on_segment(Point p,Point a,Point b)  {
    return std::abs(cross(a,b,p))<1e-9 && (p-a).dot(p-b)<=1e-9;
  }
  // 偶奇规则处理凹多边形，边上点按 spec 计入内部。
  bool inside(Point p,const Polygon& poly)  {
    bool in=false;
    for(size_t i=0,j=poly.size()-1;i<poly.size();j=i++)  {
      Point a=poly[j],b=poly[i];
      if(on_segment(p,a,b))return true;
      if((a.y>p.y)!=(b.y>p.y) && p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x)in=!in;
    }
    return in;
  }
  // 排除自交/退化，并保留顺逆绕向的合法凹多边形。
  bool valid_polygon(const Polygon& p)  {
    if(p.size()<3||std::abs(signed_area(p))<1e-10)return false;
    for(size_t i=0;i<p.size();++i)  {
      if(!std::isfinite(p[i].x)||!std::isfinite(p[i].y)||cv::norm(p[i]-p[(i+1)%p.size()])<1e-10)return false;
      for(size_t j=i+1;j<p.size();++j)  {
        if(j==i+1||(i==0&&j==p.size()-1))continue;
        Point a=p[i],b=p[(i+1)%p.size()],c=p[j],d=p[(j+1)%p.size()];
        double u=cross(a,b,c),v=cross(a,b,d),w=cross(c,d,a),z=cross(c,d,b);
        if((u*v<0&&w*z<0)||on_segment(c,a,b)||on_segment(d,a,b)||on_segment(a,c,d)||on_segment(b,c,d))return false;
      }
    }
    return true;
  }
  // 提取 Markdown 一行中有序整数坐标，不推测缺失尺寸。
  static Polygon table_points(const std::string& s)  {
    Polygon out;
    std::regex re(R"(\(([0-9]+),([0-9]+)\))");
    for(std::sregex_iterator i(s.begin(),s.end(),re),end;i!=end;++i)out.emplace_back(std::stod((*i)[1]),std::stod((*i)[2]));
    return out;
  }
  // 找到指定表格行；来源文本格式改变则显式要求重新核对转录。
  static std::vector<std::string> row(const std::string& text,const std::string& prefix)  {
    std::istringstream in(text);
    std::string line;
    while(std::getline(in,line))  {
      if(line.rfind("| "+prefix,0)!=0)continue;
      std::vector<std::string> cells;
      std::istringstream split(line);
      std::string cell;
      while(std::getline(split,cell,'|'))cells.push_back(cell);
      return cells;
    }
    throw Error(2,"model_coordinates.md 缺表格行 "+prefix);
  }
  // 比较编号固定的坐标，不能旋转或重排顶点来掩盖转录错误。
  static void equal_points(const Polygon& a,const Polygon& b,const std::string& where)  {
    if(a.size()!=b.size())throw Error(2,where+" 顶点数量不一致");
    for(size_t i=0;i<a.size();++i)if(cv::norm(a[i]-b[i])>1e-10)throw Error(2,where+" v"+std::to_string(i)+" 转录不一致");
  }
  // 模板、落位、边和锚点双重核对来源表；条件模型不升级审批状态。
  void validate_model(const Json& m,const std::string& md)  {
    if(m.at("status")!="PENDING_USER_CONFIRMATION"||m.at("unit")!="u"||m.at("stroke_width").at("value")!=8)throw Error(2,"model.json status/unit/stroke_width 与原文不符");
    std::set<std::string> ids;
    std::set<int> instances;
    std::map<std::string,Json> templates;
    for(const auto& t:m.at("templates"))  {
      std::string id=t.at("id");
      if(templates.count(id))throw Error(2,"重复模板 "+id);
      Polygon p=polygon(t.at("vertices"));
      if(!valid_polygon(p))throw Error(2,"非法模板 "+id);
      auto cells=row(md,id+" |");
      equal_points(p,table_points(cells.at(3)),id);
      auto anchors=table_points(cells.at(5));
      if(anchors.empty())throw Error(2,"模板锚点缺失 "+id);
      equal_points(polygon(Json::array(  {
        t.at("anchor")
      })),  {
        anchors[0]
      },id+" anchor");
      if(t.at("edge_lengths").size()!=p.size())throw Error(2,"模板边长缺项");
      for(size_t i=0;i<p.size();++i)if(std::abs(cv::norm(p[(i+1)%p.size()]-p[i])-t.at("edge_lengths")[i].get<double>())>1e-10)throw Error(2,"模板边长错误 "+id);
      if(!t.at("anchor_vertex").is_null()&&cv::norm(p.at(t.at("anchor_vertex").get<size_t>())-anchors[0])>1e-10)throw Error(2,"模板锚点编号错误");
      templates[id]=t;
    }
    if(templates.size()!=3||m.at("pieces").size()!=6)throw Error(2,"模型应有三模板六片");
    size_t ordinal=0;
    for(const auto& v:m.at("pieces"))  {
      std::string id=v.at("id"),type=v.at("type");
      if(!ids.insert(id).second||!instances.insert(v.at("instance_id").get<int>()).second)throw Error(2,"重复白片/实例 ID "+id);
      if(v.at("instance_id")!=++ordinal)throw Error(2,"固定实例 ID 顺序不符 "+id);
      Polygon p=polygon(v.at("vertices"));
      if(!valid_polygon(p))throw Error(2,"自交/退化多边形 "+id);
      auto cells=row(md,id+"，");
      equal_points(p,table_points(cells.at(3)),id);
      auto ap=table_points(cells.at(4));
      equal_points(polygon(Json::array(  {
        v.at("anchor")
      })),ap,id+" anchor");
      if(!inside(ap.at(0),p))throw Error(2,"锚点不在多边形上/内 "+id);
      const auto& t=templates.at(type);
      cv::Matx33d placement;
      for(int y=0;y<3;++y)for(int x=0;x<3;++x)placement(y,x)=v.at("placement")[y][x];
      equal_points(p,transform_polygon(placement,polygon(t.at("vertices"))),id+" placement");
      if(v.at("edge_ids").size()!=p.size())throw Error(2,"边编号缺失 "+id);
      for(size_t i=0;i<p.size();++i)if(v.at("edge_ids")[i]!="e"+std::to_string(i))throw Error(2,"边编号错误 "+id);
      if(v.at("source").get<std::string>().empty())throw Error(2,"缺来源 "+id);
      if(!v.at("outer").is_null())  {
        auto op=table_points(cells.at(5));
        equal_points(  {
          p.at(v.at("outer").at("vertex").get<size_t>())
        },op,id+" outer");
        if(v.at("outer").at("id")!="P"+id.substr(1,1))throw Error(2,"外角身份错误 "+id);
      }
    }
  }
  // S01：分别核验网格与负例描述；不读取模型 IO、来源或事务状态。

}
