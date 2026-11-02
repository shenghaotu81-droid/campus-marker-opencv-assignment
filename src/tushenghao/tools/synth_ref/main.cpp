
#include "vision/synth_ref/core.hpp"
#include <iostream>
#include <map>
namespace  {
  // 每个命令的帮助都说明从工具根目录运行、批准边界和退出码。
  void help()  {
    std::cout<<"Block 2 合成输入工具（不运行检测算法）\n从工具根目录执行 ./build/synthetic_gen COMMAND [options]\n" "  validate\n  plan [--run-id NAME]\n  generate --mode selftest|formal --run-id NAME [--case-id ID]\n  verify --run-id NAME\n每个命令均支持 --help。formal 需要用户提供 inputs/approval.json。\n" "退出码: 0 成功；2 输入/配置错误；3 APPROVAL_REQUIRED；4 I/O或产物验证失败。\n";
  }
  // 输入检查先于 verify 模块；已有 run 也要留下来源/配置失败报告。
  void startup_failure(const std::string& command,const std::map<std::string,std::string>& opts,const std::string& reason)  {
    try  {
      if(command=="verify"&&opts.count("--run-id"))  {
        auto path=sg::run_path(opts.at("--run-id"));
        if(sg::fs::is_directory(path)&&!sg::fs::is_symlink(path))  {
          sg::write_json(path/"verification.json",sg::Json {
             {
              "schema_version",1
            }, {
              "passed",false
            }, {
              "algorithm_validation","NOT_RUN"
            }, {
              "failure_ids",sg::Json::array()
            }, {
              "failures",sg::Json::array( {
                reason
              })
            }
          });
          std::cerr<<"报告路径: "<<sg::fs::absolute(path/"verification.json")<<'\n';
          return;
        }
      }
      std::cerr<<"未创建新的运行目录；启动前错误请看终端。\n";
    }
    catch(const std::exception& e) {
      std::cerr<<"无法保存失败报告: "<<e.what()<<'\n';
    }
  }
}
// CLI 只编排模块；错误含路径和退出含义，不隐式批准或覆盖运行。
int main(int argc,char** argv)  {
  std::string command=argc>1?argv[1]:"";
  std::map<std::string,std::string> opts;
  try  {
    if(argc<2)  {
      help();
      return 0;
    }
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--help")  {
      help();
      return 0;
    }
    if(command!="validate"&&command!="plan"&&command!="generate"&&command!="verify")throw sg::Error(2,"未知命令，查看 --help；未创建运行目录");
    for(int i=2;i<argc;++i)  {
      std::string key=argv[i];
      if((key!="--mode"&&key!="--run-id"&&key!="--case-id")||i+1>=argc||opts.count(key))throw sg::Error(2,"未知/重复/缺值选项 "+key);
      opts[key]=argv[++i];
    }
    for(const auto& item:opts)if((command=="validate")||(command=="plan"&&item.first!="--run-id")||(command=="verify"&&item.first!="--run-id"))throw sg::Error(2,"本命令不接受 "+item.first);
    sg::Context c=sg::load_context();
    if(command=="validate")  {
      sg::Json result=  {
         {
          "input_validation","PASS"
        },  {
          "config_hash",c.config_hash
        },  {
          "model_config_hash",sg::hash_text(sg::canonical(c.model))
        },  {
          "model_document_hash",sg::hash_file("inputs/model_coordinates.md")
        },  {
          "environment",sg::environment()
        }
      };
      try  {
        result["approval_record"]=sg::approval(c);
        result["formal_qualification"]="VALID";
      }
      catch(const std::exception& e)  {
        result["formal_qualification"]="BLOCKED";
        result["approval_reason"]=e.what();
      }
      std::cout<<result.dump(2)<<'\n';
      return 0;
    }
    if(command=="plan")  {
      auto name=opts.count("--run-id")?opts["--run-id"]:"plan-"+c.config_hash.substr(0,12);
      auto p=sg::plan(c,name);
      std::cout<<sg::read_text(p/"manifest.json")<<"产物路径: "<<sg::fs::absolute(p)<<'\n';
      return 0;
    }
    if(!opts.count("--run-id"))throw sg::Error(2,"缺 --run-id；未创建运行目录");
    if(command=="generate")  {
      if(!opts.count("--mode"))throw sg::Error(2,"缺 --mode；未创建运行目录");
      auto path=sg::generate(c,opts["--mode"],opts["--run-id"],opts.count("--case-id")?opts["--case-id"]:"");
      std::cout<<"生成并验证通过；产物路径: "<<sg::fs::absolute(path)<<'\n';
      return 0;
    }
    auto r=sg::verify(c,opts["--run-id"]);
    std::cout<<r.dump(2)<<'\n';
    auto report_path=sg::run_path(opts["--run-id"])/"verification.json";
    if(sg::fs::exists(report_path))std::cout<<"报告路径: "<<sg::fs::absolute(report_path)<<'\n';
    else std::cout<<"未创建运行目录；校验报告仅输出到终端。\n";
    if(!r.at("passed").get<bool>())  {
      std::cerr<<"退出码4: verify 失败，请查看 verification.json 与 failure_ids\n";
      return 4;
    }
    return 0;
  }
  catch(const sg::Error& e)  {
    startup_failure(command,opts,e.what());
    std::cerr<<"退出码"<<e.code<<": "<<e.what()<<"\n先检查 inputs/、configs/ 或运行报告；命令帮助 --help。\n";
    return e.code;
  }
  catch(const sg::Json::exception& e)  {
    startup_failure(command,opts,e.what());
    std::cerr<<"退出码2: JSON字段错误 "<<e.what()<<"；先检查 configs/ 与 inputs/；未创建运行目录\n";
    return 2;
  }
  catch(const std::exception& e)  {
    startup_failure(command,opts,e.what());
    std::cerr<<"退出码4: "<<e.what()<<"；检查空间、权限与相应run的manifest/verification。\n";
    return 4;
  }
}
