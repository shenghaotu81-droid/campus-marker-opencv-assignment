#include "vision/synth_ref/transforms.hpp"
#include "vision/synth_ref/verify.hpp"

#include "vision/synth_ref/records.hpp"
#include "vision/synth_ref/build_info.hpp"
#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <opencv2/imgcodecs.hpp>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <memory>
namespace sg {
  static Polygon clipped(Polygon input)  {
    for(int side=0;side<4;++side)  {
      Polygon output;
      if(input.empty())break;
      double limit=side==0||side==2?-0.5:(side==1?959.5:719.5);
      bool vertical=side<2,lower=side==0||side==2;
      // 一条画布边的可见侧判断，边上点保留。
      auto visible=[vertical,lower,limit](Point p)  {
        double v=vertical?p.x:p.y;
        return lower?v>=limit:v<=limit;
      };
      Point a=input.back();
      for(Point b:input)  {
        bool ia=visible(a),ib=visible(b);
        if(ia!=ib)  {
          double av=vertical?a.x:a.y,bv=vertical?b.x:b.y;
          double t=(limit-av)/(bv-av);
          output.push_back(a+(b-a)*t);
        }
        if(ib)output.push_back(b);
        a=b;
      }
      input=output;
    }
    return input;
  }
  // 连续越界判断以像素覆盖边界为准；贴边控制不等于裁切。
  static bool outside(Point p)  {
    return p.x<-0.5||p.x>959.5||p.y<-0.5||p.y>719.5;
  }
  // 几何重叠仅描述白片交集，不做类别或对应判断。
  static bool overlaps(const Polygon& a,const Polygon& b)  {
    if(a.empty()||b.empty())return false;
    for(auto p:a)if(inside(p,b))return true;
    for(auto p:b)if(inside(p,a))return true;
    for(size_t i=0;i<a.size();++i)for(size_t j=0;j<b.size();++j)  {
      Point p=a[i],q=a[(i+1)%a.size()],r=b[j],t=b[(j+1)%b.size()];
      if((q-p).cross(r-p)*(q-p).cross(t-p)<0&&(t-r).cross(p-r)*(t-r).cross(q-r)<0)return true;
    }
    return false;
  }
  // 连续面积、原始身份和可见性分开；不把不可见点变成观测点。
  static Json encode_truth_record(const Json& canvas,double model_stroke,const std::string& id,const Scene& s,const Raster* r)  {
    Json truth=  {
       {
        "schema_version",1
      },  {
        "case_id",id
      },  {
        "descriptor",s.descriptor
      },  {
        "status",s.status
      },  {
        "evidence_level",s.evidence
      },  {
        "fixture_kind",s.image?"IMAGE_AND_CONTINUOUS_GEOMETRY":"GEOMETRY_OR_COUNTER_ONLY"
      },  {
        "fixture",s.fixture
      },  {
        "pieces",Json::array()
      },  {
        "rendering",  {
           {
            "canvas",canvas
          },  {
            "subsamples",8
          },  {
            "fill","EVEN_ODD_BOUNDARY_INSIDE"
          },  {
            "pixel_centers","INTEGER"
          },  {
            "gray_rounding","floor(255*coverage/64+0.5)"
          },  {
            "mask_rule","gray>127"
          },  {
            "instance_rule","individual_coverage>0.5; overlap=65535"
          }
        }
      },  {
        "operations",s.descriptor
      },  {
        "algorithm_validation","NOT_RUN"
      }
    };
    Json overlap_pairs=Json::array();
    for(const auto& p:s.pieces)  {
      bool crop=false;
      for(auto v:p.ideal)crop|=outside(v);
      for(auto v:p.perturbed)crop|=outside(v);
      Polygon visible=p.deleted||p.invalid?Polygon  {
      }
      :clipped(p.perturbed);
      Json nominal_anchor=nullptr,perturbed_anchor=nullptr,anchor_identity=nullptr,visible_anchor=Json::array();
      if(!p.anchor.is_null())  {
        Point a=transform_point(p.transform,polygon(Json::array(  {
          p.anchor
        }))[0]);
        nominal_anchor=  {
          a.x,a.y
        };
        size_t anchor_vertex=p.type=="M"?4:3;
        anchor_identity=p.id+(p.type=="S"?".aux_center":".v"+std::to_string(anchor_vertex));
        // 已知边偏移改变凹转折位置，记录同一物理 v 编号的新交点；不能用名义点替代它。
        if(!p.invalid) {
          Point changed=p.type=="S"?a:p.perturbed.at(anchor_vertex);
          perturbed_anchor=Json::array( {
            changed.x,changed.y
          });
          if(!p.deleted&&!outside(changed)&&inside(changed,p.perturbed)) visible_anchor.push_back(Json {
             {
              "id",anchor_identity
            }, {
              "point",perturbed_anchor
            }
          });
        }
      }
      Json item=  {
         {
          "id",p.id
        },  {
          "type",p.type
        },  {
          "instance_id",p.instance
        },  {
          "source_vertices",points_json(p.source)
        },  {
          "vertex_ids",Json::array()
        },  {
          "edge_ids",p.edges
        },  {
          "model_area",std::abs(signed_area(p.source))
        },  {
          "transform",matrix_json(p.transform)
        },  {
          "ideal_vertices",points_json(p.ideal)
        },  {
          "perturbed_vertices",p.invalid?Json(nullptr):points_json(p.perturbed)
        },  {
          "perturbed_null_reason",p.invalid?Json("PERTURBATION_INVALID"):Json(nullptr)
        },  {
          "continuous_projected_area",std::abs(signed_area(p.ideal))
        },  {
          "perturbed_area",p.invalid?Json(nullptr):Json(std::abs(signed_area(p.perturbed)))
        },  {
          "source_anchor",p.anchor
        },  {
          "nominal_anchor",nominal_anchor
        },  {
          "visible_anchors",visible_anchor
        },  {
          "outer",p.outer
        },  {
          "cropped",crop
        },  {
          "designed_deleted",p.deleted
        },  {
          "invalid_perturbation",p.invalid
        },  {
          "visible_vertices",points_json(visible)
        },  {
          "visible_area",std::abs(signed_area(visible))
        }
      };
      for(size_t i=0;i<p.source.size();++i)item["vertex_ids"].push_back("v"+std::to_string(i));
      item["anchor_id"]=anchor_identity;
      item["perturbed_anchor"]=perturbed_anchor;
      bool model_piece=p.type=="L"||p.type=="M"||p.type=="S";
      item["source_unit"]=model_piece?"u":"work_px";
      // 独立矩形和新增白桥没有模型的名义笔画宽，不能伪填 8px。
      if(model_piece&&p.transform(2,0)==0&&p.transform(2,1)==0)  {
        auto widths=stroke_widths(p.transform,model_stroke);
        item["stroke_widths_px"]=widths;
        item["minimum_stroke_width_px"]=std::min(widths[0],widths[1]);
        item["singular_value_ratio"]=singular_ratio(p.transform);
      }
      else  {
        item["stroke_widths_px"]=nullptr;
        item["minimum_stroke_width_px"]=nullptr;
        item["singular_value_ratio"]=nullptr;
        item["width_null_reason"]=model_piece?"一般透视局部宽度非单一常量":"独立测试形状/干扰物无 MARK 名义笔画参数";
      }
      truth["pieces"].push_back(item);
    }
    for(size_t i=0;i<s.pieces.size();++i)for(size_t j=i+1;j<s.pieces.size();++j)  {
      const auto& a=s.pieces[i];
      const auto& b=s.pieces[j];
      if(!a.deleted&&!b.deleted&&!a.invalid&&!b.invalid&&overlaps(a.perturbed,b.perturbed))overlap_pairs.push_back(  {
        a.id,b.id
      });
    }
    truth["continuous_overlap_or_touch_pairs"]=overlap_pairs;
    truth["raster_overlap_pixels"]=r?Json(cv::countNonZero(r->instances==65535)):Json(nullptr);
    truth["raster_null_reason"]=r?Json(nullptr):Json("样例无图像；查看 status/fixture_kind");
    return truth;
  }
  Json truth_record(const Context& c,const Scene& s,const Raster* r) {
    return encode_truth_record(c.grid.at("canvas"),c.model.at("stroke_width").at("value"),case_id(c,s.descriptor),s,r);
  }
  // 安全单级名称保证所有写入在 runs 根目录，不接受 ../ 路径。

}
