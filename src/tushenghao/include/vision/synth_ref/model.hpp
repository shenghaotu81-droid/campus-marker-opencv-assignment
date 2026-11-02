#pragma once
#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
namespace sg {
  using Json=nlohmann::json;
  using Point=cv::Point2d;
  using Polygon=std::vector<Point>;
  namespace fs=std::filesystem;
  // 错误携带 CLI 退出码，区分输入、批准和产物失败。
  struct Error:std::runtime_error  {
    int code;
    // 构造携带具体退出码的异常，错误正文保留文件或 case_id。
    Error(int c,const std::string& s):std::runtime_error(s),code(c)  {
    }
  };
  struct Context  {
    Json model,grid,cases,sources;
    std::string config_hash;
    Point center;
  };
  struct Piece  {
    std::string id,type;
    int instance=0;
    Polygon source,ideal,perturbed;
    Json edges,anchor,outer;
    bool deleted=false,invalid=false;
    cv::Matx33d transform=cv::Matx33d::eye();
  };

  // S01/S02：文件输入、数值多边形与来源资格；不声明栅格或生成事务。
  std::string read_text(const fs::path&);
  Json read_json(const fs::path&);
  void write_text(const fs::path&,const std::string&);
  void write_json(const fs::path&,const Json&);
  std::string hash_text(const std::string&);
  std::string hash_file(const fs::path&);
  std::string canonical(const Json&);
  Json points_json(const Polygon&);
  Polygon polygon(const Json&);
  Json matrix_json(const cv::Matx33d&);
  double signed_area(const Polygon&);
  bool valid_polygon(const Polygon&);
  bool inside(Point,const Polygon&);
  Context load_context();
  Context load_context(const fs::path &source_root);
  void validate_model(const Json&,const std::string&);
  Json approval(const Context&);
  Json environment();
}
