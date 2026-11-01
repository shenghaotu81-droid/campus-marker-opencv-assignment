#include "synthetic_gen/transforms.hpp"

#include "synthetic_gen/model.hpp"
#include <fstream>
#include <regex>
#include <set>
#include <cmath>
#include <sstream>
namespace sg  {
  // 读原文用于逐项核对，输入文件从不被改写。
  std::string read_text(const fs::path& p)  {
    std::ifstream f(p,std::ios::binary);
    if(!f) throw Error(2,"无法读取 "+p.string());
    return  {
      std::istreambuf_iterator<char>(f),  {
      }
    };
  }
  // JSON 错误包含路径，让用户定位配置字段。
  Json read_json(const fs::path& p)  {
    try  {
      return Json::parse(read_text(p));
    }
    catch(const Error&)  {
      throw;
    }
    catch(const std::exception& e)  {
      throw Error(2,p.string()+": "+e.what());
    }
  }
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
  static void validate_grid_spec(const Json& grid) {
    const auto &g=grid;
    Json expected=  {
       {
        "seed",0
      },  {
        "canvas",  {
          960,720
        }
      },  {
        "subsamples",8
      },  {
        "threshold",127
      },  {
        "pixel_centers","INTEGER"
      },  {
        "fill","EVEN_ODD_BOUNDARY_INSIDE"
      },  {
        "background",0
      },  {
        "foreground",255
      },  {
        "ratios",  {
          1,1.5,2
        }
      },  {
        "widths",  {
          2,3,4,6,8,12
        }
      },  {
        "phases",  {
          0,0.25,0.5,0.75
        }
      },  {
        "offsets",  {
          -1,-0.5,0,0.5,1
        }
      }
    };
    for(auto i=expected.begin();i!=expected.end();++i)if(g.at(i.key())!=i.value())throw Error(2,"grid.json 字段不符合 spec: "+i.key());
    Json rotations=Json::array();
    for(int a=0;a<360;a+=15)rotations.push_back(a);
    if(g.at("rotations")!=rotations)throw Error(2,"grid.json rotations 不完整");
    Json local=  {
       {
        "rotations",  {
          0,45,90,135
        }
      },  {
        "ratios",  {
          1
        }
      },  {
        "widths",  {
          2,4,8,12
        }
      },  {
        "phases",  {
           {
            0,0
          },  {
            0.5,0.5
          }
        }
      },  {
        "offsets",  {
          -1,-0.5,0.5,1
        }
      }
    };
    Json shear=  {
       {
        "rotations",  {
          0,45,90,135
        }
      },  {
        "values",  {
          -0.5,0.5
        }
      },  {
        "widths",  {
          2,4,8,12
        }
      },  {
        "phases",  {
           {
            0,0
          },  {
            0.5,0.5
          }
        }
      }
    };
    if(g.at("local")!=local||g.at("shear")!=shear)throw Error(2,"grid.json local/shear 与 spec 不符");
    // 完整病例构造字段逐项固定，防止负例被删减后仍声称覆盖。
  }
  static void validate_case_spec(const Json& cases) {
    Json cases_expected=  {
       {
        "baseline",  {
           {
            "theta",0
          },  {
            "ratio",1
          },  {
            "width",8
          },  {
            "phase",  {
              0,0
            }
          },  {
            "offset",0
          }
        }
      },  {
        "crop_depths",  {
          0,0.5,4,12
        }
      },  {
        "bridge_widths",  {
          1,2,4
        }
      },  {
        "wrong_shift",  {
          20,20
        }
      },  {
        "white_counts",  {
          127,128,129
        }
      },  {
        "white_grid",  {
           {
            "origin",  {
              16,16
            }
          },  {
            "step",16
          },  {
            "columns",58
          },  {
            "side",8
          }
        }
      },  {
        "l_counts",  {
          15,16,17
        }
      },  {
        "l_grid",  {
           {
            "origin",  {
              24,24
            }
          },  {
            "step",24
          },  {
            "columns",38
          },  {
            "width",4
          }
        }
      },  {
        "counter_limits",  {
          560,3360,10000,64
        }
      },  {
        "narrow_widths",  {
          1,1.5
        }
      },  {
        "strong_ratios",  {
          3,4
        }
      },  {
        "strong_rotations",  {
          0,45
        }
      },  {
        "perspective_k",  {
          -0.5,0.5
        }
      },  {
        "near_collinear_offset",0.5
      }
    };
    for(auto i=cases_expected.begin();i!=cases_expected.end();++i)if(cases.at(i.key())!=i.value())throw Error(2,"cases.json 字段不符: "+i.key());
    Json blocks=Json::parse(R"([{"shape":"square","vertices":[[-8,-8],[8,-8],[8,8],[-8,8]]},{"shape":"rectangle","vertices":[[-16,-4],[16,-4],[16,4],[-16,4]]},{"shape":"triangle","vertices":[[-8,-8],[8,-8],[-8,8]]}])");
    if(cases.at("blocks")!=blocks)throw Error(2,"cases.json blocks 不符");
  }
  // 验证约定网格不能被隐式缩减；取样值仅作为实验配置。
  Context load_context(const fs::path& source_root)  {
    if(!fs::exists(source_root/"inputs/synthetic_gen_spec_Oyster_reviewed.md")||!fs::exists(source_root/"configs/model.json"))throw Error(2,"输入根目录缺少原文或模型；未创建运行目录");
    Context c;
    c.sources=read_json(source_root/"inputs/sources.json");
    std::set<std::string> source_paths;
    for(const auto& s:c.sources.at("sources"))  {
      std::string p=s.at("path");
      if(p!="inputs/model_coordinates.md"&&p!="inputs/error_budget.md"&&p!="inputs/2.md"&&p!="inputs/synthetic_gen_spec_Oyster_reviewed.md") throw Error(2,"sources.json 不允许额外来源 "+p);
      if(!source_paths.insert(p).second)throw Error(2,"sources.json 重复来源 "+p);
      if(s.at("status").get<std::string>().empty())throw Error(2,"sources.json 缺文档状态 "+p);
      if(p=="inputs/2.md"&&(s.at("original_filename")!="板块2_几何原语_完整冻结文档.md"||s.at("alias")!="2.md")) throw Error(2,"sources.json 冻结文档别名映射错误");
      if(hash_file(source_root/p)!=s.at("sha256"))throw Error(2,p+" 来源 SHA-256 不符，请检查 inputs/sources.json");
    }
    for(const std::string p:  {
      "inputs/model_coordinates.md","inputs/error_budget.md","inputs/2.md","inputs/synthetic_gen_spec_Oyster_reviewed.md"
    })if(!source_paths.count(p))throw Error(2,"sources.json 缺少 "+p);
    c.model=read_json(source_root/"configs/model.json");
    c.grid=read_json(source_root/"configs/grid.json");
    c.cases=read_json(source_root/"configs/cases.json");
    validate_model(c.model,read_text(source_root/"inputs/model_coordinates.md"));
    validate_grid_spec(c.grid);
    validate_case_spec(c.cases);
    double minx=1e9,miny=1e9,maxx=-1e9,maxy=-1e9;
    for(const auto& p:c.model.at("pieces"))for(auto v:polygon(p.at("vertices")))  {
      minx=std::min(minx,v.x);
      miny=std::min(miny,v.y);
      maxx=std::max(maxx,v.x);
      maxy=std::max(maxy,v.y);
    }
    c.center=  {
      (minx+maxx)/2,(miny+maxy)/2
    };
    c.config_hash=hash_text(canonical(Json  {
       {
        "model",c.model
      },  {
        "grid",c.grid
      },  {
        "cases",c.cases
      }
    }));
    return c;
  }
  Context load_context() { return load_context(fs::current_path()); }
  // 文本引用只能由用户提供，工具验证其绑定的三个配置/原文 hash。
  Json approval(const Context& c)  {
    if(!fs::exists("inputs/approval.json"))throw Error(3,"APPROVAL_REQUIRED: 缺 inputs/approval.json；模型/实验未获批准；未创建运行目录");
    // 记录语法或字段类型错误也是资格失败，不允许进入正式写出阶段。
    try  {
      Json a=read_json("inputs/approval.json");
      if(!a.contains("user_confirmation_quote")||!a["user_confirmation_quote"].is_string()||a["user_confirmation_quote"].get<std::string>().empty()||a.value("model_document_hash","")!=hash_file("inputs/model_coordinates.md")||a.value("experiment_config_hash","")!=c.config_hash||a.value("model_config_hash","")!=hash_text(canonical(c.model))) throw Error(3,"approval.json 用户引用或模型/实验 hash 不匹配");
      return a;
    }
    catch(const std::exception& e) {
      throw Error(3,std::string("APPROVAL_REQUIRED: ")+e.what()+"；未创建运行目录");
    }
  }
}
