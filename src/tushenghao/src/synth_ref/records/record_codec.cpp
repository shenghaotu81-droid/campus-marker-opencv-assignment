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
