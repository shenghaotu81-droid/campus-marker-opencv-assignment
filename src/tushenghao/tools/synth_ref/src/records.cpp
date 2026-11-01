#include "synthetic_gen/transforms.hpp"
#include "synthetic_gen/verify.hpp"

#include "synthetic_gen/records.hpp"
#include "synthetic_gen/build_info.hpp"
#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <opencv2/imgcodecs.hpp>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <memory>
namespace sg  {
  // EVP 使用库实现的 SHA-256，避免自写密码散列。
  std::string hash_text(const std::string& s)  {
    std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(),EVP_MD_CTX_free);
    unsigned char bytes[EVP_MAX_MD_SIZE];
    unsigned int n=0;
    if(!ctx||EVP_DigestInit_ex(ctx.get(),EVP_sha256(),nullptr)!=1||EVP_DigestUpdate(ctx.get(),s.data(),s.size())!=1||EVP_DigestFinal_ex(ctx.get(),bytes,&n)!=1)throw Error(4,"OpenSSL SHA-256 失败");
    std::ostringstream out;
    for(unsigned int i=0;i<n;++i)out<<std::hex<<std::setfill('0')<<std::setw(2)<<int(bytes[i]);
    return out.str();
  }
  // 输入文件读取失败属于输入错误；产物读取由 verify 转成验收错误。
  std::string hash_file(const fs::path& p)  {
    // 真值清单可能很大，固定大小缓冲防止结束时把全网格读入内存。
    std::ifstream file(p,std::ios::binary);
    if(!file)throw Error(2,"无法读取 "+p.string());
    std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(),EVP_MD_CTX_free);
    if(!ctx||EVP_DigestInit_ex(ctx.get(),EVP_sha256(),nullptr)!=1)throw Error(4,"OpenSSL SHA-256 初始化失败");
    std::array<char,65536> buffer;
    while(file)  {
      file.read(buffer.data(),buffer.size());
      if(EVP_DigestUpdate(ctx.get(),buffer.data(),size_t(file.gcount()))!=1) throw Error(4,"OpenSSL SHA-256 更新失败 "+p.string());
    }
    if(!file.eof())throw Error(4,"文件读取失败 "+p.string());
    unsigned char bytes[EVP_MAX_MD_SIZE];
    unsigned int n=0;
    if(EVP_DigestFinal_ex(ctx.get(),bytes,&n)!=1)throw Error(4,"OpenSSL SHA-256 结束失败");
    std::ostringstream out;
    for(unsigned int i=0;i<n;++i)out<<std::hex<<std::setfill('0')<<std::setw(2)<<int(bytes[i]);
    return out.str();
  }
  // JSON 将 NaN 默认转 null 会掩盖数值错误，因此先递归检查。
  static void finite_json(const Json& j)  {
    if(j.is_number_float()&&!std::isfinite(j.get<double>()))throw Error(2,"JSON 禁止 NaN/Infinity");
    if(j.is_structured())for(const auto& v:j)finite_json(v);
  }
  // 紧凑、排序键 JSON 是语义 hash 协议，不含空白差异。
  std::string canonical(const Json& j)  {
    finite_json(j);
    return j.dump();
  }
  // 写文件显式检查 flush 后状态，空间不足不能得到完成清单。
  void write_text(const fs::path& p,const std::string& s)  {
    std::ofstream f(p,std::ios::binary);
    if(!f)throw Error(4,"无法写入 "+p.string());
    f.write(s.data(),std::streamsize(s.size()));
    f.flush();
    if(!f)throw Error(4,"写入失败（空间/权限） "+p.string());
  }
  // 美化只影响阅读；配置 hash 始终按 canonical 计算。
  void write_json(const fs::path& p,const Json& j)  {
    finite_json(j);
    // 同目录原子替换保证中断不会把已有 INCOMPLETE 清单截成半个 JSON。
    fs::path temporary=p;
    temporary+=".tmp";
    write_text(temporary,j.dump(2)+"\n");
    fs::rename(temporary,p);
  }
  // 保持 double 坐标供连续真值，不将光栅像素替换为几何。
  Json points_json(const Polygon& p)  {
    Json j=Json::array();
    for(auto v:p)j.push_back(  {
      v.x,v.y
    });
    return j;
  }
  // 统一保存完整 3×3 仿射或单应矩阵。
  Json matrix_json(const cv::Matx33d& m)  {
    Json j=Json::array();
    for(int y=0;y<3;++y)j.push_back(  {
      m(y,0),m(y,1),m(y,2)
    });
    return j;
  }
  // 实际库版本与编译环境保留到 manifest，跨环境字节一致不在承诺内。
  Json environment()  {
    return  {
       {
        "generator_version",SG_VERSION
      },  {
        "code_hash",SG_CODE_HASH
      },  {
        "compiler",SG_COMPILER
      },  {
        "cmake_version",SG_CMAKE
      },  {
        "build_type",SG_BUILD_TYPE
      },  {
        "cxx_standard",17
      },  {
        "opencv",CV_VERSION
      },  {
        "nlohmann_json",std::to_string(NLOHMANN_JSON_VERSION_MAJOR)+"."+std::to_string(NLOHMANN_JSON_VERSION_MINOR)+"."+std::to_string(NLOHMANN_JSON_VERSION_PATCH)
      },  {
        "openssl",OpenSSL_version(OPENSSL_VERSION)
      }
    };
  }
  // 用固定画布四个半平面裁切，仅用于可见真值，不改裁切前几何。
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
  fs::path run_path(const std::string& name)  {
    if(name.empty()||name=="."||name==".."||name.size()>100)throw Error(2,"run-id 无效");
    for(unsigned char ch:name)if(!std::isalnum(ch)&&ch!='_'&&ch!='-'&&ch!='.')throw Error(2,"run-id 只接受字母数字和 ._-；未创建运行目录");
    fs::path base="runs";
    if(fs::is_symlink(base))throw Error(4,"runs 不允许符号链接");
    return base/name;
  }
  // 创建不覆盖已有目录；启动前审批和单例合法性已经完成。
  static void create_run(const fs::path& path)  {
    if(fs::exists(path)||fs::is_symlink(path))throw Error(4,"run 目录已存在，改用新 run-id: "+path.string()+"；未创建运行目录");
    fs::create_directories(path.parent_path());
    if(!fs::create_directory(path))throw Error(4,"运行目录创建失败 "+path.string());
  }
  // 计划流式保存全部 case_id，以后可选择任意已计划单例。
  fs::path plan(const Context& c,const std::string& name)  {
    auto path=run_path(name);
    create_run(path);
    Json report=  {
       {
        "schema_version",1
      },  {
        "completion_status","INCOMPLETE"
      },  {
        "config_hash",c.config_hash
      },  {
        "family_counts",Json::object()
      },  {
        "seed",0
      },  {
        "approval_status","PENDING"
      },  {
        "missing_approval",Json::array()
      }
    };
    write_json(path/"manifest.json",report);
    try  {
      std::ofstream list(path/"planned_cases.jsonl");
      if(!list)throw Error(4,"无法写计划清单");
      size_t total=0;
      Json counts=Json::object();
      // 回调只累计数量并写一行，不保留全部网格。
      enumerate(c,"formal",[&](const Json& d)  {
        std::string f=d.at("family");
        counts[f]=counts.value(f,0)+1;
        list<<canonical(Json  {
           {
            "case_id",case_id(c,d)
          },  {
            "descriptor",d
          }
        })<<'\n';
        ++total;
      });
      list.flush();
      if(!list)throw Error(4,"计划清单写入失败");
      report["family_counts"]=counts;
      report["basic_combinations"]=counts.at("global").get<size_t>()/c.grid.at("offsets").size();
      report["environment"]=environment();
      report["sources"]=c.sources;
      report["expected_cases"]=total;
      report["actual_cases"]=0;
      report["invalid_cases"]=0;
      report["total_cases"]=total;
      report["uncompressed_image_bytes_upper_bound"]=uint64_t(total)*960*720*4;
      report["upper_bound_note"]="图像平面上界（含无图像fixture的保守估计）；JSON/PNG容器和文件系统另占空间";
      try  {
        report["approval_record"]=approval(c);
        report["approval_status"]="VALID";
      }
      catch(const Error& e)  {
        report["missing_approval"].push_back(e.what());
      }
      report["completion_status"]="PLANNED";
      write_json(path/"effective_config.json",Json  {
         {
          "model",c.model
        },  {
          "grid",c.grid
        },  {
          "cases",c.cases
        }
      });
      report["planned_cases_hash"]=hash_file(path/"planned_cases.jsonl");
      write_json(path/"manifest.json",report);
      return path;
    }
    catch(const std::exception& e)  {
      report["failure"]=e.what();
      write_json(path/"manifest.json",report);
      throw;
    }
  }
  // PNG 编码策略固定，验证时用同一环境重编码比较字节摘要。
  static std::string png_bytes(const cv::Mat& m)  {
    std::vector<unsigned char> bytes;
    if(!cv::imencode(".png",m,bytes,  {
      cv::IMWRITE_PNG_COMPRESSION,3
    }))throw Error(4,"PNG 编码失败");
    return  {
      bytes.begin(),bytes.end()
    };
  }
  // CSV 字段包含 JSON 和中文时统一转义双引号。
  static std::string csv_cell(const Json& v)  {
    if(v.is_null())return "";
    std::string s=v.is_string()?v.get<std::string>():canonical(v),out="\"";
    for(char ch:s)  {
      if(ch=='\"')out+='\"';
      out+=ch;
    }
    return out+'\"';
  }
  // 输出未适用字段留空，不能用 0 冒充量测。
  static std::string index_line(const Json& t,size_t line)  {
    const auto& d=t.at("descriptor");
    const auto& files=t.at("artifacts");
    std::vector<Json> cells=  {
      t.at("case_id"),d.at("family"),t.at("status"),files.value("image",Json(nullptr)),files.value("mask",Json(nullptr)),files.value("instances",Json(nullptr)),line,d.value("theta",Json(nullptr)),d.value("ratio",Json(nullptr)),d.value("width",Json(nullptr)),d.value("phase",Json(nullptr)),d.value("offset",Json(nullptr)),t.at("evidence_level")
    };
    std::string out;
    for(const auto& cell:cells)  {
      if(!out.empty())out+=',';
      out+=csv_cell(cell);
    }
    return out+'\n';
  }
  // 固定列头单独复用，观测模板不写任何测量数据。
  static const std::string index_header="case_id,family,status,image,mask,instance_mask,truth_line,theta,ratio,stroke_width,phase,offset,evidence_level\n";
  static const std::string observation_header="case_id,algorithm_version,config_hash,metric,value,unit,status,reason,evidence_path\n";
  // 生成事务先写 INCOMPLETE；任何中断都不能自动恢复为 COMPLETE。
  fs::path generate(const Context& c,const std::string& mode,const std::string& name,const std::string& only)  {
    Json a=nullptr;
    if(mode=="formal")a=approval(c);
    else if(mode!="selftest")throw Error(2,"mode 无效；未创建运行目录");
    bool found=only.empty();
    size_t expected=0;
    Json families=Json::object();
    // 正式资格和 ID 检查在创建目录前完成。
    enumerate(c,mode,[&](const Json& d)  {
      if(!only.empty()&&case_id(c,d)!=only)return;
      found=true;
      ++expected;
      std::string family=d.at("family");
      families[family]=families.value(family,0)+1;
    });
    if(!found)throw Error(2,"未知 case-id "+only+"；先运行 plan；未创建运行目录");
    auto path=run_path(name);
    create_run(path);
    Json manifest=  {
       {
        "schema_version",1
      },  {
        "environment",environment()
      },  {
        "sources",c.sources
      },  {
        "config_hash",c.config_hash
      },  {
        "approval_record",a
      },  {
        "mode",mode
      },  {
        "seed",0
      },  {
        "subset",!only.empty()
      },  {
        "selected_case_id",only.empty()?Json(nullptr):Json(only)
      },  {
        "expected_cases",expected
      },  {
        "actual_cases",0
      },  {
        "invalid_cases",0
      },  {
        "family_counts",families
      },  {
        "completion_status","INCOMPLETE"
      },  {
        "algorithm_validation","NOT_RUN"
      }
    };
    write_json(path/"manifest.json",manifest);
    size_t actual=0,invalid=0;
    try  {
      for(const std::string dir:  {
        "images","masks","instance_masks"
      })fs::create_directory(path/dir);
      write_json(path/"effective_config.json",Json  {
         {
          "model",c.model
        },  {
          "grid",c.grid
        },  {
          "cases",c.cases
        }
      });
      write_text(path/"observation_template.csv",observation_header);
      std::ofstream truth(path/"truth.jsonl"),index(path/"index.csv");
      if(!truth||!index)throw Error(4,"无法打开 truth/index");
      index<<index_header;
      // 每次只生成一个场景和三张图，内存与完整网格数量无关。
      enumerate(c,mode,[&](const Json& d)  {
        std::string id=case_id(c,d);
        if(!only.empty()&&id!=only)return;
        try  {
          Scene scene=make_scene(c,d);
          Raster r;
          Json artifacts=Json::object(),hashes=Json::object();
          if(scene.image)  {
            r=rasterize(scene);
            check_scene(scene,r);
            std::array<cv::Mat,3> mats=  {
              r.image,r.mask,r.instances
            };
            std::array<std::string,3> keys=  {
              "image","mask","instances"
            },dirs=  {
              "images","masks","instance_masks"
            };
            for(int i=0;i<3;++i)  {
              std::string rel=dirs[i]+"/"+id+".png",bytes=png_bytes(mats[i]);
              write_text(path/rel,bytes);
              artifacts[keys[i]]=rel;
              hashes[keys[i]]=hash_text(bytes);
            }
          }
          Json t=truth_record(c,scene,scene.image?&r:nullptr);
          t["semantic_hash"]=hash_text(canonical(t));
          t["artifacts"]=artifacts;
          t["artifact_hashes"]=hashes;
          truth<<canonical(t)<<'\n';
          index<<index_line(t,actual+1);
          if(!truth||!index)throw Error(4,"truth/index 写入失败");
          ++actual;
          if(scene.status=="PERTURBATION_INVALID")++invalid;
        }
        catch(const std::exception& e)  {
          throw Error(4,"case_id="+id+": "+e.what());
        }
      });
      truth.flush();
      index.flush();
      if(!truth||!index)throw Error(4,"truth/index flush 失败");
      truth.close();
      index.close();
      manifest["actual_cases"]=actual;
      manifest["invalid_cases"]=invalid;
      // 全部文件写完也暂不宣告完成，先做独立校验。
      manifest["completion_status"]="INCOMPLETE";
      manifest["file_hashes"]=  {
         {
          "truth.jsonl",hash_file(path/"truth.jsonl")
        },  {
          "index.csv",hash_file(path/"index.csv")
        },  {
          "effective_config.json",hash_file(path/"effective_config.json")
        },  {
          "observation_template.csv",hash_file(path/"observation_template.csv")
        }
      };
      write_json(path/"manifest.json",manifest);
      auto report=verify(c,name,true,true);
      if(!report.at("passed").get<bool>())throw Error(4,"自动 verify 失败，查看 "+(path/"verification.json").string());
      manifest["completion_status"]="COMPLETE";
      write_json(path/"manifest.json",manifest);
      return path;
    }
    catch(const std::exception& e)  {
      manifest["completion_status"]="INCOMPLETE";
      manifest["actual_cases"]=actual;
      manifest["invalid_cases"]=invalid;
      manifest["failure"]=e.what();
      try  {
        write_json(path/"manifest.json",manifest);
      }
      catch(...)  {
        /* 空间/权限失败可能连清单都无法更新，磁盘上初始状态仍是不完整。 */
      }
      throw Error(4,path.string()+": "+e.what());
    }
  }
  // 提供协议 helper 给独立 verify 复用，避免复制实现。
  std::string record_index_line(const Json& t,size_t line)  {
    return index_line(t,line);
  }
  // 返回固定索引列头供校验使用，不接受隐藏附加列。
  std::string record_index_header()  {
    return index_header;
  }
  // 返回只有列名的观测模板，算法接入前禁止测量行。
  std::string record_observation_header()  {
    return observation_header;
  }
  // 校验重编码使用相同 PNG 策略，比较真实字节而非只信文件名。
  std::string record_png_bytes(const cv::Mat& m)  {
    return png_bytes(m);
  }
}
