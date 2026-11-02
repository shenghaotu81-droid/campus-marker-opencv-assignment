
#include "vision/synth_ref/core.hpp"
#include <opencv2/imgcodecs.hpp>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <set>
#include <fstream>
#include <cmath>
using namespace sg;
namespace  {
  // 证据跟随测试二进制所在的构建目录，支持从干净 build-review 验收。
  struct TestPaths { fs::path source_root, evidence_root; };
  struct ScopedDirectory {
    fs::path previous=fs::current_path();
    explicit ScopedDirectory(const fs::path &path) { fs::current_path(path); }
    ~ScopedDirectory() { std::error_code error; fs::current_path(previous,error); }
  };
  // 显式检查在 Release 同样执行，并说明失败的验收性质。
  void require(bool condition,const std::string& reason)  {
    if(!condition)throw std::runtime_error(reason);
  }
  // 浮点容差只验证双精度计算，不输出给检测器或用作观测误差预算。
  void near(double a,double b,const std::string& reason,double epsilon=1e-8)  {
    require(std::abs(a-b)<=epsilon*std::max(1.0,std::abs(b)),reason);
  }
  // 预期错误必须真正抛出，不能用禁用的 assert 掩盖失败。
  void rejects(const std::function<void()>& action,const std::string& reason,int code=0)  {
    bool rejected=false;
    try  {
      action();
    }
    catch(const Error& e)  {
      rejected=code==0||e.code==code;
    }
    catch(const std::exception&)  {
      rejected=code==0;
    }
    require(rejected,reason);
  }
  // 测试只构造独立矩形，防止自测借用未批准 MARK 模型。
  Scene shape(const Polygon& poly,int instance=1)  {
    Scene s;
    s.descriptor=  {
       {
        "family","selftest"
      }
    };
    Piece p;
    p.id="independent";
    p.instance=instance;
    p.type="INDEPENDENT_TEST_SHAPE";
    p.source=p.ideal=p.perturbed=poly;
    p.anchor=nullptr;
    p.outer=nullptr;
    p.edges=Json::array();
    s.pieces.push_back(p);
    return s;
  }
  // A3：全部表格转录对照、缺项、重复身份、自交与计算面积。
  void model_test()  {
    auto c=load_context();
    validate_model(c.model,read_text("inputs/model_coordinates.md"));
    for(const auto& p:c.model.at("pieces"))  {
      double computed=std::abs(signed_area(polygon(p.at("vertices"))));
      std::string type=p.at("type");
      if(type=="L")near(computed,30*30-22*22,"L 面积推导");
      if(type=="M")near(computed,14*14-6*6,"M 条件面积由顶点推导");
      if(type=="S")near(computed,8*8,"S 面积推导");
    }
    auto broken=c.model;
    broken["pieces"][0].erase("anchor");
    rejects([&]()  {
      validate_model(broken,read_text("inputs/model_coordinates.md"));
    },"缺锚点应拒绝");
    broken=c.model;
    broken["pieces"][1]["id"]="L0";
    rejects([&]()  {
      validate_model(broken,read_text("inputs/model_coordinates.md"));
    },"重复 ID 应拒绝");
    broken=c.model;
    std::swap(broken["pieces"][0]["vertices"][1],broken["pieces"][0]["vertices"][4]);
    rejects([&]()  {
      validate_model(broken,read_text("inputs/model_coordinates.md"));
    },"自交应拒绝");
    broken=c.model;
    broken["pieces"][0]["edge_ids"][2]="e3";
    rejects([&]()  {
      validate_model(broken,read_text("inputs/model_coordinates.md"));
    },"边编号错位应拒绝");
    broken=c.model;
    broken["templates"][0]["edge_lengths"][0]=31;
    rejects([&]()  {
      validate_model(broken,read_text("inputs/model_coordinates.md"));
    },"边长转录错误应拒绝");
    rejects([]()  {
      canonical(Json(std::numeric_limits<double>::infinity()));
    },"非有限 JSON 必须拒绝");
  }
  // A4：已知矩阵、顺时针方向、逆转置尺度、面积和奇异分支。
  void transforms_test()  {
    auto c=load_context();
    for(int theta=0;theta<360;theta+=15)for(double r:  {
      1.0,1.5,2.0
    })for(double width:  {
      2.,3.,4.,6.,8.,12.
    })  {
      Json d=  {
         {
          "theta",theta
        },  {
          "ratio",r
        },  {
          "width",width
        },  {
          "phase",  {
            0.25,0.75
          }
        }
      };
      auto m=affine(c,d);
      auto widths=stroke_widths(m,8);
      near(std::min(widths[0],widths[1]),width,"笔画最小宽符合网格");
      Point center=transform_point(m,c.center);
      near(center.x,480.25,"中心 x/相位");
      near(center.y,360.75,"中心 y/相位");
      for(const auto& p:c.model.at("pieces"))  {
        auto poly=polygon(p.at("vertices"));
        near(std::abs(signed_area(transform_polygon(m,poly))),std::abs(cv::determinant(cv::Mat(m))*signed_area(poly)),"仿射面积缩放",1e-7);
      }
    }
    auto m=affine(c,Json  {
       {
        "theta",90
      },  {
        "ratio",1
      },  {
        "width",8
      }
    });
    Point q=transform_point(m,c.center+Point(1,0));
    near(q.x,480,"90 度 x");
    near(q.y,361,"正角显示顺时针");
    auto shear=affine(c,Json  {
       {
        "theta",45
      },  {
        "shear",0.5
      },  {
        "width",4
      }
    });
    require(singular_ratio(shear)>1,"剪切奇异值比");
    rejects([]()  {
      stroke_widths(cv::Matx33d::zeros(),8);
    },"奇异矩阵不能求逆");
    rejects([]()  {
      transform_point(cv::Matx33d::zeros(),Point(1,2));
    },"零透视分母应拒绝");
  }
  // A4：每个 ID 唯一，基本组合与子网格数量完整，禁止静默削减。
  void grid_test()  {
    auto c=load_context();
    std::map<std::string,int> counts;
    std::set<std::string> ids;
    std::set<std::string> bases;
    enumerate(c,"formal",[&](const Json& d)  {
      ++counts[d.at("family")];
      require(ids.insert(case_id(c,d)).second,"重复 case_id");
      if(d.at("family")=="global"&&d.at("offset")==0)  {
        Json base=d;
        base.erase("offset");
        bases.insert(canonical(base));
      }
    });
    require(counts["global"]==34560&&bases.size()==6912,"基本/全局数量");
    require(counts["local"]==4096&&counts["shear"]==64,"局部/剪切数量");
    require(ids.size()==38784,"完整样例数量");
    require(counts["crop"]==16&&counts["missing"]==7&&counts["bridge"]==6&&counts["counter"]==12,"负例覆盖数量");
    Json first=  {
       {
        "family","selftest"
      },  {
        "shape","rectangle_integer"
      }
    };
    require(case_id(c,first)==case_id(c,Json::parse(canonical(first))),"键顺序与 JSON 往返不能改变身份");
  }
  // A5：独立矩形整数/半像素手算、凹部、并集和实例重叠。
  void raster_test()  {
    auto s=shape(  {
       {
        2,2
      },  {
        4,2
      },  {
        4,4
      },  {
        2,4
      }
    });
    auto r=rasterize(s,8,8);
    require(r.image.at<uint8_t>(3,3)==255,"中心全覆盖");
    require(r.image.at<uint8_t>(2,3)==128&&r.mask.at<uint8_t>(2,3)==255,"半覆盖灰度128且通过严格阈值");
    require(r.instances.at<uint16_t>(2,3)==0,"实例半覆盖不通过 >0.5");
    require(r.image.at<uint8_t>(2,2)==64&&r.mask.at<uint8_t>(2,2)==0,"角落四分之一覆盖");
    auto half=rasterize(shape(  {
       {
        1.5,1.5
      },  {
        3.5,1.5
      },  {
        3.5,3.5
      },  {
        1.5,3.5
      }
    }),8,8);
    require(cv::countNonZero(half.mask)==4&&half.image.at<uint8_t>(2,2)==255,"半像素矩形恰好四个像素");
    auto l=rasterize(shape(  {
       {
        1.5,1.5
      },  {
        5.5,1.5
      },  {
        5.5,2.5
      },  {
        2.5,2.5
      },  {
        2.5,5.5
      },  {
        1.5,5.5
      }
    }),8,8);
    require(l.mask.at<uint8_t>(4,4)==0&&l.mask.at<uint8_t>(2,4)==255,"L 凹部不填平");
    s.pieces.push_back(s.pieces[0]);
    s.pieces.back().instance=2;
    auto overlap=rasterize(s,8,8);
    require(overlap.instances.at<uint16_t>(3,3)==65535&&overlap.image.at<uint8_t>(3,3)==255,"重叠实例与灰度并集");
    for(int y=0;y<8;++y)for(int x=0;x<8;++x)require((r.mask.at<uint8_t>(y,x)==255)==(r.image.at<uint8_t>(y,x)>127),"mask 严格对应灰度");
  }
  // A6：顺逆绕向正负偏移、单边不漂移及坍缩保留。
  void perturb_test()  {
    Polygon p=  {
       {
        0,0
      },  {
        4,0
      },  {
        4,4
      },  {
        0,4
      }
    },out;
    for(int reverse=0;reverse<2;++reverse)  {
      require(offset_polygon(p,  {
        1,1,1,1
      },out),"膨胀有效");
      near(std::abs(signed_area(out)),36,"膨胀面积");
      require(offset_polygon(p,  {
        -1,-1,-1,-1
      },out),"收缩有效");
      near(std::abs(signed_area(out)),4,"收缩面积");
      require(!offset_polygon(p,  {
        -2,-2,-2,-2
      },out)&&out.empty(),"坍缩不能修复");
      require(!offset_polygon(p,  {
        -3,-3,-3,-3
      },out),"穿越骨架不能恢复合法");
      std::reverse(p.begin(),p.end());
    }
    p=  {
       {
        0,0
      },  {
        4,0
      },  {
        4,4
      },  {
        0,4
      }
    };
    require(offset_polygon(p,  {
      1,0,0,0
    },out),"局部单边有效");
    near(out[0].y,-1,"只移动上边");
    near(out[1].x,4,"右侧支撑线不漂移");
    near(out[2].y,4,"下边不漂移");
    near(out[3].x,0,"左边不漂移");
    auto c=load_context();
    auto invalid=make_scene(c,Json  {
       {
        "family","selftest"
      },  {
        "shape","rectangle_invalid"
      }
    });
    require(invalid.status=="PERTURBATION_INVALID"&&!invalid.image,"非法扰动保留记录");
    auto t=truth_record(c,invalid,nullptr);
    require(t["pieces"][0]["perturbed_vertices"].is_null(),"非法几何不伪造顶点");
    // 有效局部偏移要保留原始锚点身份，位置取已知新交点而非名义点。
    auto local=make_scene(c,Json {
       {
        "family","local"
      }, {
        "theta",0
      }, {
        "ratio",1
      }, {
        "width",8
      }, {
        "phase", {
          0,0
        }
      }, {
        "offset",0.5
      }, {
        "piece_id","L0"
      }, {
        "edge_index",2
      }, {
        "edge_id","e2"
      }
    });
    auto changed=truth_record(c,local,nullptr).at("pieces")[0];
    require(changed.at("anchor_id")=="L0.v3","锚点物理身份保持");
    near(changed.at("perturbed_anchor")[1],changed.at("nominal_anchor")[1].get<double>()+0.5,"扰动锚点取支撑线新交点");
    require(!changed.at("visible_anchors").empty(),"有效偏移不能丢失仍可见的锚点");
  }
  // A7：仅检查定义的几何 fixture，不执行未批准的 MARK 图像实验。
  void cases_test()  {
    auto c=load_context();
    size_t n=0;
    Json counts=Json::object();
    enumerate_special(c,[&](const Json& d)  {
      ++n;
      auto s=make_scene(c,d);
      std::string f=d.at("family");
      counts[f]=counts.value(f,0)+1;
      auto t=truth_record(c,s,nullptr);
      require(t.at("algorithm_validation")=="NOT_RUN","负例不能替算法下结论");
      if(f=="crop")  {
        bool cropped=false;
        for(const auto& p:t["pieces"])cropped|=p.at("cropped").get<bool>();
        require(cropped==(d.at("depth").get<double>()>0),"四侧贴边不冒充裁切");
      }
      if(f=="missing")  {
        int deleted=0;
        for(const auto& p:t["pieces"])if(p.at("designed_deleted").get<bool>())  {
          ++deleted;
          require(p.at("visible_vertices").empty()&&p.at("visible_anchors").empty(),"设计删除必须无可见真值");
        }
        require(deleted==(d.at("delete")=="ALL"?6:1),"缺片数量");
      }
      if(f=="ordinary")require(s.pieces.size()==1&&s.pieces[0].type=="NO_MARK_SEMANTICS","普通白块无 MARK 身份");
      if(f=="bridge")require(valid_polygon(s.pieces.back().perturbed)&&s.pieces.back().type=="ADDED_WHITE_BRIDGE","桥多边形构造");
      if(f=="permutations"||f=="symmetric")require(s.fixture.at("candidate_assignments").size()==6,"六种已知锚点关系");
      if(f=="symmetric")require(s.pieces.empty()&&s.evidence=="INSUFFICIENT_EVIDENCE","对称 fixture 省略独立证据");
      if(f=="counter")require(!s.image&&s.evidence=="COUNTER_SCENARIO_ONLY","计数描述不能冒充图像");
      if(f=="collinear")require(s.fixture.at("degenerate")==true&&s.fixture.at("inverse_evaluated")==false,"共线不得继续逆矩阵");
      if(f=="mirror")require(cv::determinant(cv::Mat(s.pieces[0].transform))<0,"镜像行列式");
      if(f=="narrow"||f=="strong_affine"||f=="perspective")require(s.status=="OUT_OF_GRID","范围外单独记录");
    });
    require(n==64,"§5 共64项");
    // 用独立矩形验证连通检查本身，避免未批准模型实验。
    auto squares=shape(  {
       {
        12,12
      },  {
        20,12
      },  {
        20,20
      },  {
        12,20
      }
    });
    squares.descriptor=  {
       {
        "family","white_pressure"
      },  {
        "count",2
      }
    };
    auto second=shape(  {
       {
        28,12
      },  {
        36,12
      },  {
        36,20
      },  {
        28,20
      }
    },2).pieces[0];
    second.id="second";
    squares.pieces.push_back(second);
    check_scene(squares,rasterize(squares,48,48));
    squares.descriptor["count"]=3;
    rejects([&]()  {
      check_scene(squares,rasterize(squares,48,48));
    },"压力连通数必须实际吻合",4);
    auto joined=shape(  {
       {
        4,4
      },  {
        8,4
      },  {
        8,8
      },  {
        4,8
      }
    });
    joined.pieces[0].id="a";
    auto b=shape(  {
       {
        12,4
      },  {
        16,4
      },  {
        16,8
      },  {
        12,8
      }
    },2).pieces[0];
    b.id="b";
    joined.pieces.push_back(b);
    joined.descriptor=  {
       {
        "family","bridge"
      }
    };
    joined.fixture=  {
       {
        "joined_piece_ids",  {
          "a","b"
        }
      }
    };
    rejects([&]()  {
      check_scene(joined,rasterize(joined,24,24));
    },"断开的桥接必须失败",4);
    joined.pieces.push_back(shape(  {
       {
        8,5.5
      },  {
        12,5.5
      },  {
        12,6.5
      },  {
        8,6.5
      }
    },3).pieces[0]);
    check_scene(joined,rasterize(joined,24,24));
  }
  // 测试目录隔离复制只读输入；无有效批准记录，仅造明确错误的审批负例。
  void workflow_test(const TestPaths& paths)  {
    const auto entry=fs::current_path();
    try { ScopedDirectory guard(fs::temp_directory_path()); throw std::runtime_error("fixture interrupt"); }
    catch(const std::runtime_error&) {}
    require(fs::current_path()==entry,"中断后必须恢复调用方目录");
    require(load_context(paths.source_root).config_hash==load_context().config_hash,"显式根加载身份不一致");
    auto original=paths.source_root;
    auto stamp=std::chrono::high_resolution_clock::now().time_since_epoch().count();
    fs::path temp=fs::temp_directory_path()/("sg-selftest-"+std::to_string(stamp));
    fs::create_directory(temp);
    fs::copy(original/"inputs",temp/"inputs",fs::copy_options::recursive);
    fs::copy(original/"configs",temp/"configs",fs::copy_options::recursive);
    if(fs::exists(temp/"inputs/approval.json"))fs::remove(temp/"inputs/approval.json");
    ScopedDirectory working_directory(temp);
    Json evidence;
    try  {
      auto c=load_context(temp);
      rejects([&]()  {
        generate(c,"formal","no-approval");
      },"无批准正式运行必须退出3",3);
      require(!fs::exists("runs/no-approval"),"无批准不能创建run");
      write_json("inputs/approval.json",Json  {
         {
          "user_confirmation_quote","TEST_INVALID_NOT_APPROVAL"
        },  {
          "model_document_hash","wrong"
        },  {
          "model_config_hash","wrong"
        },  {
          "experiment_config_hash","wrong"
        }
      });
      rejects([&]()  {
        generate(c,"formal","bad-approval");
      },"hash不符必须退出3",3);
      require(!fs::exists("runs/bad-approval"),"错误批准不能创建run");
      fs::remove("inputs/approval.json");
      auto a=generate(c,"selftest","repeat-a"),b=generate(c,"selftest","repeat-b");
      require(hash_file(a/"truth.jsonl")==hash_file(b/"truth.jsonl"),"两次规范化真值 hash 必须一致");
      for(const auto& f:fs::directory_iterator(a/"images"))require(hash_file(f.path())==hash_file(b/"images"/f.path().filename()),"两次 PNG hash 必须一致");
      require(verify(c,"repeat-a").at("passed")==true,"完整 verify 必须通过");
      rejects([&]()  {
        generate(c,"selftest","repeat-a");
      },"已有run不得覆盖",4);
      Json chosen;
      // 独立形状也测试单例 subset，不能冒充完整网格。
      enumerate(c,"selftest",[&](const Json& d)  {
        if(chosen.is_null())chosen=d;
      });
      auto subset=generate(c,"selftest","subset-test",case_id(c,chosen));
      auto subset_manifest=read_json(subset/"manifest.json");
      require(subset_manifest.at("subset")==true&&subset_manifest.at("actual_cases")==1,"单例必须明确 subset");
      require(verify(c,"subset-test").at("passed")==true,"单例 verify 应通过");
      // 损坏副本中一个 PNG 字节，检查不能只相信清单里保存的 hash。
      auto subset_image=fs::directory_iterator(subset/"images")->path();
      auto damaged=read_text(subset_image);
      damaged[0]=char(damaged[0]^1);
      write_text(subset_image,damaged);
      require(verify(c,"subset-test").at("passed")==false,"PNG hash 损坏必须失败");
      rejects([&]()  {
        generate(c,"selftest","unknown-case","bad-id");
      },"未知 case-id 应拒绝",2);
      require(!fs::exists("runs/unknown-case"),"未知 ID 不创建目录");
      auto p=plan(c,"plan-test");
      require(read_json(p/"manifest.json").at("total_cases")==38784,"plan 总数");
      require(read_text(a/"observation_template.csv").find('\n')==read_text(a/"observation_template.csv").size()-1,"观测模板只有列头");
      auto image=fs::directory_iterator(b/"images")->path();
      std::string removed_id=image.stem().string();
      fs::remove(image);
      auto failed=verify(c,"repeat-b");
      require(failed.at("passed")==false,"删除图像必须失败");
      require(failed.at("failure_ids").size()==1,"单个缺图不能导致清单错位的假失败");
      require(std::find(failed["failure_ids"].begin(),failed["failure_ids"].end(),Json(removed_id))!=failed["failure_ids"].end(),"删图失败必须给case_id");
      auto manifest=read_json(a/"manifest.json");
      manifest["completion_status"]="INCOMPLETE";
      write_json(a/"manifest.json",manifest);
      require(verify(c,"repeat-a").at("passed")==false,"INCOMPLETE不能通过");
      evidence=  {
         {
          "temporary_root",temp.string()
        },  {
          "approval_missing","PASS_EXIT_3_NO_RUN"
        },  {
          "approval_bad_hash","PASS_EXIT_3_NO_RUN"
        },  {
          "repeat_truth_hash",hash_file(b/"truth.jsonl")
        },  {
          "repeat_png_hashes_equal",true
        },  {
          "deleted_image_case_id",removed_id
        },  {
          "deleted_image_verification",failed
        },  {
          "incomplete_rejected",true
        },  {
          "algorithm_validation","NOT_RUN"
        }
      };
      fs::current_path(original);
      evidence["subset_verified_before_corruption"]=true;
      evidence["png_hash_corruption_rejected"]=true;
      write_json(paths.evidence_root/"test-evidence-workflow.json",evidence);
      std::cout<<"证据: "<<(paths.evidence_root/"test-evidence-workflow.json")<<"；临时运行: "<<temp<<'\n';
    }
    catch(...)  {
      fs::current_path(original);
      throw;
    }
  }
}
// CTest 单独选择测试名，使错误可定位到对应验收项。
int main(int argc,char** argv)  {
  try  {
    const TestPaths paths{SG_SOURCE_ROOT,fs::absolute(argv[0]).parent_path()};
    ScopedDirectory source_directory(paths.source_root);
    if(argc!=2)throw std::runtime_error("需要测试名");
    std::string name=argv[1];
    if(name=="model")model_test();
    else if(name=="transforms")transforms_test();
    else if(name=="grid")grid_test();
    else if(name=="raster")raster_test();
    else if(name=="perturb")perturb_test();
    else if(name=="cases")cases_test();
    else if(name=="workflow")workflow_test(paths);
    else throw std::runtime_error("未知测试名");
    std::cout<<name<<" PASS\n";
    return 0;
  }
  catch(const std::exception& e)  {
    std::cerr<<"FAIL: "<<e.what()<<'\n';
    return 1;
  }
}
