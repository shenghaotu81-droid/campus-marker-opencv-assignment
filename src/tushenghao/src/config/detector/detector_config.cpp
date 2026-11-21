#include "vision/config/detector_config.hpp"
#include "reader/value_reader.hpp"
#include <stdexcept>
#include <filesystem>
#include <opencv2/core.hpp>

namespace mark::detector
{
    // 检测配置只提供分割（preprocess）、结构匹配（geometry）和物理角到屏幕角的安装映射（corner）。
    detector::DetectorConfig load_detector_config(const std::filesystem::path &path)
    {
        // OpenCV 专门读写 YAML/XML/JSON 的文件存储类（打开文件、解析、按 key 取节点）；只读打开
        cv::FileStorage fs(path.string(), cv::FileStorage::READ);
        // 文件打不开（路径错、文件不存在）抛错
        if (!fs.isOpened())
            throw std::runtime_error("CONFIG_OPEN_FAILED:" + path.string());
        // 默认构造（带默认值）
        detector::DetectorConfig d;
        // 空节点 → static_cast 直接转出 0 → 覆盖掉 struct 默认值
        // 按层级取 YAML 节点
        d.preprocess_ = {config_value::integer(fs["preprocess"]["work_width"]),
                         config_value::integer(fs["preprocess"]["work_height"]),
                         config_value::integer(fs["preprocess"]["threshold"])};
        d.geometry_ = {config_value::integer(fs["geometry"]["white_threshold"]),
                       config_value::number(fs["geometry"]["approximation_epsilon"])};  // approximation_epsilon 是 approxPolyDP 的拟合误差
        const auto mapping = fs["corner"]["physical_to_screen"];
        for (int i = 0; i < 4; ++i)
            d.corner_.physical_to_screen_[i] = config_value::integer(mapping[i]);
        return d;
    }
}
