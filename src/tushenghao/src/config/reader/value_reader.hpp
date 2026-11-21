#pragma once
#include <opencv2/core.hpp>
#include <string>

namespace mark::config_value
{
    // 将 YAML 节点转换为模块配置值；字段内容由配置维护者确定。
    // 三个函数只是 static_cast 的命名包装，不做校验；
    // 命名的意义是让调用处读起来像“读配置值”，改动时只改这一处。
    // FileNode 是 OpenCV 里表示 YAML/XML 文件中一个节点的类型。
    inline std::string text(const cv::FileNode &node)
    {
        return static_cast<std::string>(node);
    }

    inline int integer(const cv::FileNode &node)
    {
        return static_cast<int>(node);
    }

    inline double number(const cv::FileNode &node)
    {
        return static_cast<double>(node);
    }
}
