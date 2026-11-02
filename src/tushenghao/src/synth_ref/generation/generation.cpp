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
      write_text(path/"observation_template.csv",record_observation_header());
      std::ofstream truth(path/"truth.jsonl"),index(path/"index.csv");
      if(!truth||!index)throw Error(4,"无法打开 truth/index");
      index<<record_index_header();
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
              std::string rel=dirs[i]+"/"+id+".png",bytes=record_png_bytes(mats[i]);
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
          index<<record_index_line(t,actual+1);
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

}
