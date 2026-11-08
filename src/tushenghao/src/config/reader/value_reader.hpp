#pragma once
#include <opencv2/core.hpp>
#include <string>

namespace mark::config_value
{
    // 将 YAML 节点转换为模块配置值；字段内容由配置维护者确定。
    inline std::string text(const cv::FileNode &node) { return static_cast<std::string>(node); }
    inline int integer(const cv::FileNode &node) { return static_cast<int>(node); }
    inline double number(const cv::FileNode &node) { return static_cast<double>(node); }
}
