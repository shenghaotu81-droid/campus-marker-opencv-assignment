#pragma once
#include "vision/synth_ref/model.hpp"
namespace sg {
// S04：已知变换作用于连续几何，不估计检测器参数。
Point transform_point(const cv::Matx33d&,Point);
Polygon transform_polygon(const cv::Matx33d&,const Polygon&);
cv::Matx33d affine(const Context&,const Json&);
std::array<double,2> stroke_widths(const cv::Matx33d&,double);
double singular_ratio(const cv::Matx33d&);
}
