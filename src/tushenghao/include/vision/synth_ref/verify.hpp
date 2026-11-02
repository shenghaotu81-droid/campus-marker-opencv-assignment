#pragma once
#include "vision/synth_ref/records.hpp"
namespace sg {
// S08：独立重建语义真值与字节身份；公开包含路径保持兼容。
Json verify(const Context&,const std::string&,bool save=true,bool pending_generation=false);
}
