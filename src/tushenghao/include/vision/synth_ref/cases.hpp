#pragma once
#include "vision/synth_ref/grid.hpp"
namespace sg {
  struct Scene  {
    Json descriptor,fixture=Json::object();
    std::vector<Piece> pieces;
    bool image=true;
    std::string status="GENERATED",evidence="CONTINUOUS_AND_RASTER_INPUT";
  };

struct Raster;
// S04/S05：显式描述生成理想几何与负例，真值与输入扰动分别保存。
void enumerate_special(const Context&,const Visitor&);
Scene make_scene(const Context&,const Json&);
void apply_special(const Context&,Scene&);
void check_scene(const Scene&,const Raster&);
}
