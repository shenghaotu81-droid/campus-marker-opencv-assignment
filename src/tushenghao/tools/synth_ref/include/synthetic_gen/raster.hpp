#pragma once
#include "synthetic_gen/cases.hpp"
namespace sg {
  struct Raster  {
    cv::Mat image,mask,instances;
  };

// S05：固定子点覆盖规则，只消费场景，不读取总配置。
Raster rasterize(const Scene&,int width=960,int height=720);
}
