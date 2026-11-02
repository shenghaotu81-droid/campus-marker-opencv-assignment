#include "vision/synth_ref/transforms.hpp"

#include "vision/synth_ref/model.hpp"
#include <fstream>
#include <regex>
#include <set>
#include <cmath>
#include <sstream>
namespace sg {
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
