#include "vision/synth_ref/cases.hpp"

#include "vision/synth_ref/grid.hpp"
namespace sg  {
  // 身份只含语义配置与确定性描述，运行目录/时间不影响 ID。
  std::string case_id(const Context& c,const Json& d)  {
    return hash_text(canonical(Json  {
       {
        "config_hash",c.config_hash
      },  {
        "case",d
      }
    }));
  }
  // 按 spec §7 第三步枚举，各子网格独立；基本组合在全局 offset=0 中完整保留。
  void enumerate(const Context& c,const std::string& mode,const Visitor& visit)  {
    if(mode=="selftest")  {
      for(const std::string shape:  {
        "rectangle_integer","rectangle_half","l_concave","rectangle_inflated","rectangle_invalid"
      })visit(Json  {
         {
          "family","selftest"
        },  {
          "shape",shape
        }
      });
      return;
    }
    if(mode!="formal")throw Error(2,"mode 应为 selftest/formal");
    const auto& g=c.grid;
    size_t base=0;
    for(const auto& theta:g.at("rotations"))for(const auto& r:g.at("ratios"))for(const auto& w:g.at("widths"))for(const auto& px:g.at("phases"))for(const auto& py:g.at("phases"))  {
      for(const auto& offset:g.at("offsets"))visit(Json  {
         {
          "family","global"
        },  {
          "base_index",base
        },  {
          "theta",theta
        },  {
          "ratio",r
        },  {
          "width",w
        },  {
          "phase",  {
            px,py
          }
        },  {
          "offset",offset
        }
      });
      ++base;
    }
    const auto& l=g.at("local");
    for(const auto& theta:l.at("rotations"))for(const auto& w:l.at("widths"))for(const auto& phase:l.at("phases"))for(const auto& p:c.model.at("pieces"))for(size_t edge=0;edge<p.at("vertices").size();++edge)for(const auto& offset:l.at("offsets"))visit(Json  {
       {
        "family","local"
      },  {
        "theta",theta
      },  {
        "ratio",1
      },  {
        "width",w
      },  {
        "phase",phase
      },  {
        "offset",offset
      },  {
        "piece_id",p.at("id")
      },  {
        "edge_id","e"+std::to_string(edge)
      },  {
        "edge_index",edge
      }
    });
    const auto& sh=g.at("shear");
    for(const auto& theta:sh.at("rotations"))for(const auto& h:sh.at("values"))for(const auto& w:sh.at("widths"))for(const auto& phase:sh.at("phases"))visit(Json  {
       {
        "family","shear"
      },  {
        "theta",theta
      },  {
        "ratio",1
      },  {
        "shear",h
      },  {
        "width",w
      },  {
        "phase",phase
      },  {
        "offset",0
      }
    });
    enumerate_special(c,visit);
  }
}
