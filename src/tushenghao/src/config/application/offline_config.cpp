#include "vision/config/app_config.hpp"
#include "reader/value_reader.hpp"
#include <stdexcept>
#include <opencv2/core.hpp>
#include <filesystem>

namespace mark
{
    // 读取输出目录、视频文件名和叠加开关，缺省项使用 OfflineRunConfig 默认值。
    OfflineRunConfig load_offline_config(const std::filesystem::path &path)
    {
        cv::FileStorage fs(path.string(), cv::FileStorage::READ);
        if (!fs.isOpened())
            throw std::runtime_error("OFFLINE_CONFIG_OPEN_FAILED:" + path.string());
        OfflineRunConfig c;
        // 缺键行为：空节点 → 跳过赋值 → 留 struct 默认值
        if (!fs["run_directory"].empty())
            c.run_directory_ = config_value::text(fs["run_directory"]);
        if (!fs["video_filename"].empty())
            c.video_filename_ = config_value::text(fs["video_filename"]);
        if (!fs["overlay"].empty())
            c.overlay_ = config_value::text(fs["overlay"]) == "true";  // OpenCV 的 FileNode 没有布尔类型，类型只有 NONE/INT/REAL/STR/SEQ/MAP。此处YAML的 true 读进来按字符串 "true" 存的
        return c;
    }
}
