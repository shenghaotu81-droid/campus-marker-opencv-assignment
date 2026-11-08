#include "vision/config/detector_config.hpp"
#include "reader/value_reader.hpp"
#include <stdexcept>

namespace mark::detector
{
    // 检测配置只提供分割、结构匹配和物理角到屏幕角的安装映射。
    detector::DetectorConfig load_detector_config(const std::filesystem::path &path)
    {
        cv::FileStorage fs(path.string(), cv::FileStorage::READ);
        if (!fs.isOpened())
            throw std::runtime_error("CONFIG_OPEN_FAILED:" + path.string());
        detector::DetectorConfig d;
        d.preprocess_ = {config_value::integer(fs["preprocess"]["work_width"]),
                         config_value::integer(fs["preprocess"]["work_height"]),
                         config_value::integer(fs["preprocess"]["threshold"])};
        d.geometry_ = {config_value::integer(fs["geometry"]["white_threshold"]),
                       config_value::number(fs["geometry"]["approximation_epsilon"])};
        const auto mapping = fs["corner"]["physical_to_screen"];
        for (int i = 0; i < 4; ++i)
            d.corner_.physical_to_screen_[i] = config_value::integer(mapping[i]);
        return d;
    }
}
