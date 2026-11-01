#include "synthetic_gen/transforms.hpp"
#include "synthetic_gen/perturb.hpp"
#include "synthetic_gen/raster.hpp"

#include "synthetic_gen/cases.hpp"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
namespace sg  {
  // 添加一个确定性特殊样例，基准参数来自 cases.json。
  static Json special(const Json& cases,const std::string& family)  {
    Json d=cases.at("baseline");
    d["family"]=family;
    return d;
  }
  // §5 每行显式枚举，几何与计数描述也有 case_id，但不伪造图像。
  static void enumerate_special_values(const Json& model,const Json& cases,const Visitor& visit)  {
    for(const std::string side:  {
      "left","right","top","bottom"
    })for(const auto& depth:cases.at("crop_depths"))  {
      auto d=special(cases,"crop");
      d["side"]=side;
      d["depth"]=depth;
      visit(d);
    }
    for(const auto& p:model.at("pieces"))  {
      auto d=special(cases,"missing");
      d["delete"]=Json::array(  {
        p.at("id")
      });
      visit(d);
    }
    auto d=special(cases,"missing");
    d["delete"]="ALL";
    visit(d);
    for(const auto& b:cases.at("blocks"))  {
      d=Json {
         {
          "family","ordinary"
        }
      };
      d["block"]=b;
      visit(d);
    }
    for(const std::string target:  {
      "S1a","S1b"
    })for(const auto& w:cases.at("bridge_widths"))  {
      d=special(cases,"bridge");
      d["target"]=target;
      d["bridge_width"]=w;
      visit(d);
    }
    for(const std::string f:  {
      "wrong_combination","permutations","symmetric","mirror","collinear","near_collinear"
    })  {
      // 共线描述没有图像仿射参数，不能把基准值填成观测。
      if(f=="collinear"||f=="near_collinear")visit(Json {
         {
          "family",f
        }
      });
      else visit(special(cases,f));
    }
    for(const auto& n:cases.at("white_counts"))  {
      d=Json {
         {
          "family","white_pressure"
        }
      };
      d["count"]=n;
      visit(d);
    }
    for(const auto& n:cases.at("l_counts"))  {
      d=Json {
         {
          "family","l_pressure"
        }, {
          "width",cases.at("l_grid").at("width")
        }
      };
      d["count"]=n;
      visit(d);
    }
    for(const auto& n:cases.at("counter_limits"))for(int delta:  {
      -1,0,1
    })  {
      d=  {
         {
          "family","counter"
        },  {
          "limit_label",n
        },  {
          "count",n.get<int>()+delta
        }
      };
      visit(d);
    }
    for(const auto& w:cases.at("narrow_widths"))  {
      d=special(cases,"narrow");
      d["width"]=w;
      visit(d);
    }
    for(const auto& r:cases.at("strong_ratios"))for(const auto& theta:cases.at("strong_rotations"))  {
      d=special(cases,"strong_affine");
      d["ratio"]=r;
      d["theta"]=theta;
      d["width"]=4;
      visit(d);
    }
    for(const auto& k:cases.at("perspective_k"))  {
      d=special(cases,"perspective");
      d["k"]=k;
      visit(d);
    }
  }
  void enumerate_special(const Context& c,const Visitor& visit) {
    enumerate_special_values(c.model,c.cases,visit);
  }
  // 自测片不含 MARK 标签或模型尺寸，保持未批准时的独立性。
  static Piece independent(const std::string& id,const Polygon& vertices,int instance,const std::string& type="INDEPENDENT_TEST_SHAPE")  {
    Piece p;
    p.id=id;
    p.type=type;
    p.instance=instance;
    p.source=p.ideal=p.perturbed=vertices;
    p.anchor=nullptr;
    p.outer=nullptr;
    p.edges=Json::array();
    for(size_t i=0;i<vertices.size();++i)p.edges.push_back("e"+std::to_string(i));
    return p;
  }
  // 构造理想几何，先保留名义真值再做扰动，二者不相互替代。
  static Scene make_scene_values(const Json& model,Point model_center,const Json& d,cv::Matx33d m)  {
    Scene s;
    s.descriptor=d;
    std::string family=d.at("family");
    if(family=="selftest")  {
      std::string shape=d.at("shape");
      Polygon v=  {
         {
          478,358
        },  {
          482,358
        },  {
          482,362
        },  {
          478,362
        }
      };
      if(shape=="rectangle_half")for(auto& p:v)p+=Point(0.5,0.5);
      if(shape=="l_concave")v=  {
         {
          476,356
        },  {
          484,356
        },  {
          484,358
        },  {
          478,358
        },  {
          478,364
        },  {
          476,364
        }
      };
      s.pieces.push_back(independent("test_shape",v,1));
      if(shape=="rectangle_inflated"||shape=="rectangle_invalid")  {
        double offset=shape=="rectangle_invalid"?-2:0.5;
        auto& p=s.pieces[0];
        if(!offset_polygon(p.ideal,std::vector<double>(v.size(),offset),p.perturbed))  {
          p.invalid=true;
          s.image=false;
          s.status="PERTURBATION_INVALID";
        }
        s.fixture["offset"]=offset;
      }
      return s;
    }
    if(family=="counter")  {
      s.image=false;
      s.evidence=s.status="COUNTER_SCENARIO_ONLY";
      s.fixture=  {
         {
          "fixture_kind","COUNTER_SCENARIO_ONLY"
        },  {
          "requested_count",d.at("count")
        },  {
          "proposed_limit_label",d.at("limit_label")
        },  {
          "external_audit_required",true
        },  {
          "reason","必须由外部 audit 构造实际执行路径，生成器不提供内部计数实测"
        }
      };
      return s;
    }
    if(family=="mirror")  {
      cv::Matx33d reflection(-1,0,2*model_center.x,0,1,0,0,0,1);
      m=m*reflection;
    }
    if(family=="perspective")  {
      double k=d.at("k");
      cv::Matx33d center(1,0,-model_center.x,0,1,-model_center.y,0,0,1),project(1,0,0,0,1,0,k/80,0,1),finish(1,0,480,0,1,360,0,0,1);
      m=finish*project*center;
      s.fixture["denominator_finite_checked"]=true;
    }
    for(const auto& v:model.at("pieces"))  {
      Piece p;
      p.id=v.at("id");
      p.type=v.at("type");
      p.instance=v.at("instance_id");
      p.source=polygon(v.at("vertices"));
      p.edges=v.at("edge_ids");
      p.anchor=v.at("anchor");
      p.outer=v.at("outer");
      p.transform=m;
      p.ideal=transform_polygon(m,p.source);
      std::vector<double> offsets(p.source.size(),0);
      if(family=="global")std::fill(offsets.begin(),offsets.end(),d.at("offset").get<double>());
      if(family=="local"&&p.id==d.at("piece_id"))offsets.at(d.at("edge_index").get<size_t>())=d.at("offset");
      if(!offset_polygon(p.ideal,offsets,p.perturbed))  {
        p.invalid=true;
        s.image=false;
        s.status="PERTURBATION_INVALID";
      }
      s.pieces.push_back(p);
    }
    return s;
  }
  Scene make_scene(const Context& c,const Json& d) {
    const std::string family=d.at("family");
    const bool base_only=family=="selftest"||family=="counter";
    auto s=make_scene_values(c.model,c.center,d,base_only?cv::Matx33d::eye():affine(c,d));
    if(!base_only)apply_special(c,s);
    return s;
  }
  // 对理想和扰动顶点同时平移，保存完整矩阵让裁切前坐标仍可复核。
  static void translate_piece(Piece& p,Point delta)  {
    for(auto& v:p.ideal)v+=delta;
    for(auto& v:p.perturbed)v+=delta;
    cv::Matx33d shift(1,0,delta.x,0,1,delta.y,0,0,1);
    p.transform=shift*p.transform;
  }
  // 构造已知三点坐标基底；只是解析列举关系，不从图像提取或拟合仿射。
  static cv::Matx33d basis(const std::array<Point,3>& p)  {
    return  {
      p[1].x-p[0].x,p[2].x-p[0].x,p[0].x,p[1].y-p[0].y,p[2].y-p[0].y,p[0].y,0,0,1
    };
  }
  // 六种候选关系只基于三个已知锚点，绝不声称独立证据验证通过。
  static Json permutations(const std::array<Point,3>& source,const std::array<Point,3>& observed)  {
    Json result=Json::array();
    std::array<int,3> order=  {
      0,1,2
    };
    cv::Matx33d b=basis(source);
    if(std::abs(cv::determinant(cv::Mat(b)))<1e-12)throw Error(2,"候选关系基底退化");
    do  {
      std::array<Point,3> target=  {
        observed[order[0]],observed[order[1]],observed[order[2]]
      };
      result.push_back(Json  {
         {
          "assignment",order
        },  {
          "true_assignment",order==std::array<int,3>  {
            0,1,2
          }
        },  {
          "known_anchor_relation",matrix_json(basis(target)*b.inv())
        },  {
          "independent_validation","NOT_RUN"
        }
      });
    }
    while(std::next_permutation(order.begin(),order.end()));
    return result;
  }
  // 应用负例构造，不输出 REFUSED/RESOURCE_LIMIT 或任何检测结果。
  static void apply_special_values(const Json& model,const Json& cases,Scene& s)  {
    const auto& d=s.descriptor;
    std::string f=d.at("family");
    if(f=="narrow"||f=="strong_affine"||f=="perspective")s.status="OUT_OF_GRID";
    if(f=="crop")  {
      double depth=d.at("depth");
      std::string side=d.at("side");
      double minx=1e9,maxx=-1e9,miny=1e9,maxy=-1e9;
      for(const auto& p:s.pieces)for(auto v:p.ideal)  {
        minx=std::min(minx,v.x);
        maxx=std::max(maxx,v.x);
        miny=std::min(miny,v.y);
        maxy=std::max(maxy,v.y);
      }
      Point delta;
      if(side=="left")delta.x=-0.5-depth-minx;
      if(side=="right")delta.x=959.5+depth-maxx;
      if(side=="top")delta.y=-0.5-depth-miny;
      if(side=="bottom")delta.y=719.5+depth-maxy;
      for(auto& p:s.pieces)translate_piece(p,delta);
      s.fixture=  {
         {
          "side",side
        },  {
          "depth_px",depth
        },  {
          "touching_control",depth==0
        }
      };
    }
    if(f=="missing")for(auto& p:s.pieces)  {
      if(d.at("delete")=="ALL")p.deleted=true;
      else for(const auto& id:d.at("delete"))if(p.id==id)p.deleted=true;
    }
    if(f=="ordinary")  {
      s.pieces.clear();
      Polygon p=polygon(d.at("block").at("vertices"));
      for(auto& v:p)v+=Point(480,360);
      s.pieces.push_back(independent("distractor",p,1,"NO_MARK_SEMANTICS"));
    }
    if(f=="bridge")  {
      const auto& m=s.pieces.at(3);
      const auto& small=s.pieces.at(d.at("target")=="S1a"?4:5);
      double width=d.at("bridge_width");
      Polygon b;
      if(d.at("target")=="S1a")  {
        double x0=small.ideal[1].x,x1=m.ideal[0].x,cy=(small.ideal[0].y+small.ideal[2].y)/2;
        b=  {
           {
            x0,cy-width/2
          },  {
            x1,cy-width/2
          },  {
            x1,cy+width/2
          },  {
            x0,cy+width/2
          }
        };
      }
      else  {
        double y0=m.ideal[2].y,y1=small.ideal[0].y,cx=(small.ideal[0].x+small.ideal[1].x)/2;
        b=  {
           {
            cx-width/2,y0
          },  {
            cx+width/2,y0
          },  {
            cx+width/2,y1
          },  {
            cx-width/2,y1
          }
        };
      }
      s.pieces.push_back(independent("bridge",b,7,"ADDED_WHITE_BRIDGE"));
      s.fixture=  {
         {
          "joined_piece_ids",  {
            "M1",d.at("target")
          }
        },  {
          "bridge_polygon",points_json(b)
        }
      };
    }
    if(f=="wrong_combination")  {
      Point delta(cases.at("wrong_shift")[0],cases.at("wrong_shift")[1]);
      for(auto& p:s.pieces)if(p.type!="L")translate_piece(p,delta);
      s.fixture["applied_shift"]=cases.at("wrong_shift");
    }
    if(f=="permutations"||f=="symmetric")  {
      std::array<Point,3> src,obs;
      for(int i=0;i<3;++i)  {
        src[i]=polygon(Json::array(  {
          s.pieces[i].anchor
        }))[0];
        obs[i]=transform_point(s.pieces[i].transform,src[i]);
      }
      s.fixture=  {
         {
          "anchor_ids",  {
            "L0.v3","L2.v3","L3.v3"
          }
        },  {
          "anchors",points_json(  {
            obs[0],obs[1],obs[2]
          })
        },  {
          "candidate_assignments",permutations(src,obs)
        }
      };
      s.image=false;
      s.evidence="GEOMETRY_FIXTURE_ONLY";
      if(f=="symmetric")  {
        s.pieces.clear();
        s.status=s.evidence="INSUFFICIENT_EVIDENCE";
        s.fixture["omitted_evidence"]=Json::array(  {
          "ALL_INDEPENDENT_EDGES","SEGMENTED_CORNER"
        });
      }
    }
    if(f=="collinear"||f=="near_collinear")  {
      s.image=false;
      s.pieces.clear();
      s.evidence="GEOMETRY_FIXTURE_ONLY";
      double y=f=="near_collinear"?cases.at("near_collinear_offset").get<double>():0;
      s.fixture=  {
         {
          "anchors",  {
             {
              448,360
            },  {
              512,360
            },  {
              480,360+y
            }
          }
        },  {
          "anchor_ids",  {
            "L0.v3","L2.v3","L3.v3"
          }
        },  {
          "triangle_area",32*y
        },  {
          "degenerate",y==0
        },  {
          "inverse_evaluated",false
        },  {
          "near_collinear_threshold",nullptr
        },  {
          "reason","退化/近退化留外部算法检验，不引入拒绝门限"
        }
      };
    }
    if(f=="white_pressure"||f=="l_pressure")  {
      s.pieces.clear();
      int n=d.at("count");
      const auto& grid=cases.at(f=="white_pressure"?"white_grid":"l_grid");
      int columns=grid.at("columns");
      double step=grid.at("step"),ox=grid.at("origin")[0],oy=grid.at("origin")[1];
      for(int i=0;i<n;++i)  {
        double x=ox+step*(i%columns),y=oy+step*(i/columns);
        Polygon p;
        if(f=="white_pressure")  {
          double half=grid.at("side").get<double>()/2;
          p=  {
             {
              x-half,y-half
            },  {
              x+half,y-half
            },  {
              x+half,y+half
            },  {
              x-half,y+half
            }
          };
        }
        else  {
          p=polygon(model.at("templates")[0].at("vertices"));
          double scale=grid.at("width").get<double>()/model.at("stroke_width").at("value").get<double>();
          for(auto& v:p)v=Point(x,y)+(v-Point(15,15))*scale;
        }
        s.pieces.push_back(independent("pressure_"+std::to_string(i),p,i+1,f=="white_pressure"?"INDEPENDENT_SQUARE":"GENERATED_L_TEMPLATE"));
      }
      s.fixture=  {
         {
          "generated_piece_count",n
        },  {
          "grid_order","c=i%columns, r=i/columns"
        },  {
          "algorithm_candidate_count",nullptr
        },  {
          "resource_limit_approved",false
        }
      };
    }
  }
  void apply_special(const Context& c,Scene& s) {
    apply_special_values(c.model,c.cases,s);
  }
  // 检查二值输入的连通事实，避免桥接构造未连上仍报告成功。
  void check_scene(const Scene& s,const Raster& r)  {
    std::string f=s.descriptor.at("family");
    if(f=="white_pressure"||f=="bridge")  {
      cv::Mat labels;
      int count=cv::connectedComponents(r.mask,labels,8,CV_32S)-1;
      if(f=="white_pressure"&&count!=s.descriptor.at("count").get<int>())throw Error(4,"白片压力实际连通域数不符");
      if(f=="bridge")  {
        const auto& ids=s.fixture.at("joined_piece_ids");
        int joined=-1;
        for(const auto& p:s.pieces)if(p.id==ids[0]||p.id==ids[1])  {
          int label=0;
          for(int y=0;y<r.mask.rows && !label;++y)for(int x=0;x<r.mask.cols;++x)if(r.instances.at<uint16_t>(y,x)==p.instance)  {
            label=labels.at<int>(y,x);
            break;
          }
          if(label==0|| (joined!=-1&&label!=joined))throw Error(4,"桥接 mask 未实际连通");
          joined=label;
        }
      }
    }
  }
}
