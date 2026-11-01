#pragma once
#include "synthetic_gen/model.hpp"
#include <functional>
namespace sg {
// S03：流式产出描述与稳定身份，不保留全量网格。
using Visitor=std::function<void(const Json&)>;
void enumerate(const Context&,const std::string&,const Visitor&);
std::string case_id(const Context&,const Json&);
}
