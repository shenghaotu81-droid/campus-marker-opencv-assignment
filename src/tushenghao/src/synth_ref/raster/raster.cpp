
#include "vision/synth_ref/raster.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace sg  {
  // 统计 64 位子像素占用，覆盖并集不能简单相加灰度。
  static int bit_count(uint64_t bits)  {
    return __builtin_popcountll(bits);
  }
  // 精确实现固定采样公式；按每片包围盒加速，但不修改连续顶点。
  Raster rasterize(const Scene& s,int w,int h)  {
    Raster out  {
      cv::Mat::zeros(h,w,CV_8UC1),cv::Mat::zeros(h,w,CV_8UC1),cv::Mat::zeros(h,w,CV_16UC1)
    };
    std::vector<uint64_t> coverage(size_t(w)*h,0);
    for(const auto& piece:s.pieces)  {
      if(piece.deleted||piece.invalid||piece.perturbed.empty())continue;
      const auto& poly=piece.perturbed;
      double minx=poly[0].x,maxx=minx,miny=poly[0].y,maxy=miny;
      for(auto p:poly)  {
        minx=std::min(minx,p.x);
        maxx=std::max(maxx,p.x);
        miny=std::min(miny,p.y);
        maxy=std::max(maxy,p.y);
      }
      int x0=int(std::max(0.0,std::min(double(w),std::ceil(minx-0.5)))),x1=int(std::max(-1.0,std::min(double(w-1),std::floor(maxx+0.5)))),y0=int(std::max(0.0,std::min(double(h),std::ceil(miny-0.5)))),y1=int(std::max(-1.0,std::min(double(h-1),std::floor(maxy+0.5))));
      for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)  {
        uint64_t bits=0;
        for(int j=0;j<8;++j)for(int i=0;i<8;++i)if(inside(Point(x-0.5+(i+0.5)/8,y-0.5+(j+0.5)/8),poly))bits|=uint64_t(1)<<(j*8+i);
        coverage[size_t(y)*w+x]|=bits;
        if(bit_count(bits)>32)  {
          auto& id=out.instances.at<uint16_t>(y,x);
          if(id==0)id=uint16_t(piece.instance);
          else if(id!=piece.instance)id=65535;
        }
      }
    }
    // 灰度四舍五入与严格大于 127 的 mask 固定，不能搜索阈值。
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)  {
      int value=(255*bit_count(coverage[size_t(y)*w+x])+32)/64;
      out.image.at<uint8_t>(y,x)=uint8_t(value);
      out.mask.at<uint8_t>(y,x)=value>127?255:0;
    }
    return out;
  }
}
