#include "vision/synth_ref/transforms.hpp"

#include "vision/synth_ref/verify.hpp"
#include <opencv2/imgcodecs.hpp>
#include <fstream>
#include <set>
#include <cmath>
namespace sg  {
  // 协议 helper 由 records 模块实现；验证仍独立重建场景与文件内容。
  // 检查清单、独立覆盖和每个 PNG，错误集中报告并带 case_id。
  Json verify(const Context& c,const std::string& name,bool save,bool pending_generation)  {
    auto path=run_path(name);
    Json report=  {
       {
        "schema_version",1
      },  {
        "passed",false
      },  {
        "algorithm_validation","NOT_RUN"
      },  {
        "failure_ids",Json::array()
      },  {
        "failures",Json::array()
      },  {
        "checks",  {
           {
            "coverage",false
          },  {
            "hash_consistency",false
          },  {
            "geometry_area",false
          },  {
            "repeat_generation",false
          },  {
            "png_dimensions_types",false
          },  {
            "negative_constructions",false
          }
        }
      }
    };
    try  {
      if(!fs::is_directory(path)||fs::is_symlink(path))throw Error(4,"run 不存在或为符号链接 "+path.string());
      Json manifest=read_json(path/"manifest.json");
      // 内部生成事务在验收完之前始终保持 INCOMPLETE；公开 verify 不接受它。
      if(manifest.at("completion_status")!="COMPLETE" && !(pending_generation && manifest.at("completion_status")=="INCOMPLETE")) throw Error(4,"manifest 为 INCOMPLETE/PLANNED，不可冒充完整生成");
      if(manifest.at("config_hash")!=c.config_hash||manifest.at("sources")!=c.sources)throw Error(4,"来源/配置 hash 不一致");
      if(manifest.at("environment")!=environment())throw Error(4,"生成环境/代码版本与当前二进制不一致；请使用原环境验证");
      if(manifest.at("seed")!=0||manifest.at("algorithm_validation")!="NOT_RUN")throw Error(4,"seed/algorithm_validation 不符");
      Json effective=  {
         {
          "model",c.model
        },  {
          "grid",c.grid
        },  {
          "cases",c.cases
        }
      };
      if(read_json(path/"effective_config.json")!=effective)throw Error(4,"effective_config.json 不符");
      if(read_text(path/"observation_template.csv")!=record_observation_header())throw Error(4,"观测模板不能预填测量行");
      for(const std::string file:  {
        "truth.jsonl","index.csv","effective_config.json","observation_template.csv"
      })if(manifest.at("file_hashes").at(file)!=hash_file(path/file))report["failures"].push_back("文件 hash 不符 "+file);
      std::string mode=manifest.at("mode"),only=manifest.at("selected_case_id").is_null()?"":manifest.at("selected_case_id").get<std::string>();
      if(manifest.at("subset").get<bool>()!=!only.empty())throw Error(4,"subset/selected_case_id 不一致");
      if(mode=="formal")  {
        const auto& a=manifest.at("approval_record");
        if(a.value("user_confirmation_quote","").empty()||a.value("model_document_hash","")!=hash_file("inputs/model_coordinates.md")||a.value("model_config_hash","")!=hash_text(canonical(c.model))||a.value("experiment_config_hash","")!=c.config_hash)throw Error(4,"manifest 批准绑定错误");
      }
      else if(!manifest.at("approval_record").is_null())throw Error(4,"selftest 不应声明正式批准");
      std::ifstream truth(path/"truth.jsonl"),index(path/"index.csv");
      if(!truth||!index)throw Error(4,"truth/index 缺文件");
      std::string header;
      std::getline(index,header);
      if(header+'\n'!=record_index_header())throw Error(4,"index.csv 列头错误");
      size_t count=0,invalid=0;
      Json family_counts=Json::object();
      std::set<std::string> ids,expected_images,expected_masks,expected_instances;
      // 重新枚举全部预期项，流式读取逐行对应，漏样/重排/重复都失败。
      enumerate(c,mode,[&](const Json& d)  {
        std::string id=case_id(c,d);
        if(!only.empty()&&id!=only)return;
        ++count;
        std::string family=d.at("family");
        family_counts[family]=family_counts.value(family,0)+1;
        // 同时消费两张清单的当前行，单个缺图不能制造后续行错位的假失败。
        std::string line,csv;
        bool truth_line=bool(std::getline(truth,line)),csv_line=bool(std::getline(index,csv));
        try  {
          Scene scene=make_scene(c,d);
          if(scene.status=="PERTURBATION_INVALID")++invalid;
          if(scene.image) {
            expected_images.insert(id+".png");
            expected_masks.insert(id+".png");
            expected_instances.insert(id+".png");
          }
          if(!truth_line)throw Error(4,"缺 truth 行");
          Json t=Json::parse(line);
          if(t.at("case_id")!=id||!ids.insert(id).second)throw Error(4,"case_id 重复/枚举顺序错误");
          Raster r;
          if(scene.image)  {
            r=rasterize(scene);
            check_scene(scene,r);
          }
          else if(!t.at("artifacts").empty())throw Error(4,"无图像 fixture 不能伪造图片");
          Json expected=truth_record(c,scene,scene.image?&r:nullptr);
          if(t.at("semantic_hash")!=hash_text(canonical(expected)))throw Error(4,"独立重建语义 hash 不符");
          Json semantic=t;
          semantic.erase("artifacts");
          semantic.erase("artifact_hashes");
          semantic.erase("semantic_hash");
          if(canonical(semantic)!=canonical(expected))throw Error(4,"真值与独立重建不符");
          // 面积恒等式只用于非退化仿射，透视面积直接由投影顶点计算。
          for(const auto& p:scene.pieces)  {
            if(p.transform(2,0)==0&&p.transform(2,1)==0)  {
              double det=p.transform(0,0)*p.transform(1,1)-p.transform(0,1)*p.transform(1,0),area=std::abs(signed_area(p.ideal)),expected_area=std::abs(det*signed_area(p.source));
              if(std::abs(area-expected_area)>1e-7*std::max(1.0,expected_area))throw Error(4,"连续面积缩放不符 "+p.id);
            }
          }
          if(scene.image)  {
            if(t.at("artifacts").size()!=3||t.at("artifact_hashes").size()!=3)throw Error(4,"图像 artifact 缺项");
            std::array<cv::Mat,3> mats=  {
              r.image,r.mask,r.instances
            };
            std::array<std::string,3> keys=  {
              "image","mask","instances"
            },dirs=  {
              "images","masks","instance_masks"
            };
            for(int i=0;i<3;++i)  {
              std::string rel=dirs[i]+"/"+id+".png";
              if(t.at("artifacts").at(keys[i])!=rel)throw Error(4,"artifact 路径不符 "+keys[i]);
              if(fs::is_symlink(path/rel)||!fs::is_regular_file(path/rel))throw Error(4,"缺文件/非法链接 "+rel);
              std::string bytes=read_text(path/rel),hash=hash_text(bytes);
              if(hash!=t.at("artifact_hashes").at(keys[i]))throw Error(4,"PNG hash 不符 "+rel);
              if(hash!=hash_text(record_png_bytes(mats[i])))throw Error(4,"重生成 PNG hash 不符 "+rel);
              cv::Mat png=cv::imread((path/rel).string(),cv::IMREAD_UNCHANGED);
              if(png.size()!=cv::Size(960,720)||png.type()!=(i==2?CV_16UC1:CV_8UC1))throw Error(4,"PNG 尺寸/类型不符 "+rel);
              if(cv::countNonZero(png!=mats[i])!=0)throw Error(4,"PNG 像素不符 "+rel);
            }
          }
          if(!csv_line||csv+'\n'!=record_index_line(t,count))throw Error(4,"index.csv 行不符");
        }
        catch(const std::exception& e)  {
          report["failure_ids"].push_back(id);
          report["failures"].push_back("case_id="+id+": "+e.what());
        }
      });
      std::string tail;
      if(std::getline(truth,tail)||std::getline(index,tail))report["failures"].push_back("truth/index 多余行");
      if(count==0||manifest.at("expected_cases")!=count||manifest.at("actual_cases")!=count||manifest.at("invalid_cases")!=invalid||manifest.at("family_counts")!=family_counts)report["failures"].push_back("清单数量/覆盖不符");
      // 图片目录也核对多余文件，防止 subset 混入其他样例。
      std::array<std::string,3> dirs=  {
        "images","masks","instance_masks"
      };
      std::array<std::set<std::string>*,3> sets=  {
        &expected_images,&expected_masks,&expected_instances
      };
      for(int i=0;i<3;++i)  {
        std::set<std::string> actual;
        for(const auto& entry:fs::directory_iterator(path/dirs[i]))actual.insert(entry.path().filename().string());
        if(actual!=*sets[i])report["failures"].push_back(dirs[i]+" 文件集合不符");
      }
      report["coverage_counts"]=family_counts;
      report["expected_cases"]=count;
      report["invalid_cases"]=invalid;
      report["subset"]=!only.empty();
      report["passed"]=report["failures"].empty();
      for(auto& check:report["checks"].items())check.value()=report["passed"];
      report["repeat_method"]="由描述独立重建全部连续真值与PNG，比较规范化语义hash和编码hash";
    }
    catch(const std::exception& e)  {
      report["failures"].push_back(e.what());
    }
    if(save&&fs::is_directory(path)&&!fs::is_symlink(path))write_json(path/"verification.json",report);
    return report;
  }
}
