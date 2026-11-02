#pragma once
#include "vision/synth_ref/raster.hpp"
namespace sg {
// S06/S07：编码只消费明确场景与栅格，事务入口保持旧调用签名。
Json truth_record(const Context&,const Scene&,const Raster*);
fs::path plan(const Context&,const std::string&);
fs::path generate(const Context&,const std::string&,const std::string&,const std::string& only="");
fs::path run_path(const std::string&);
// 格式编码是 records/verify 的共同契约，独立真值验证不复用生成器判定。
std::string record_index_line(const Json&,size_t);
std::string record_index_header();
std::string record_observation_header();
std::string record_png_bytes(const cv::Mat&);
}
