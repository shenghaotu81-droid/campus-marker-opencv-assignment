#include "detector/config.hpp"
#include "reader/value_reader.hpp"
#include "reader/config_error.hpp"
#include <algorithm>
#include <set>

namespace mark
{
    namespace
    {
        void fields(const cv::FileNode &node, std::initializer_list<const char *> names)
        {
            if (!node.isMap())
                throw ConfigError("CONFIG_EXPECT_MAP");

            std::set<std::string> allowed(names.begin(), names.end()), seen;

            for (auto it = node.begin(); it != node.end(); ++it)
                if (!allowed.count((*it).name()) || !seen.insert((*it).name()).second)
                    throw ConfigError("CONFIG_UNKNOWN_OR_DUPLICATE_KEY:" + (*it).name());
        }

        void bad(config_value::Error)
        {
            throw ConfigError("CONFIG_VALUE_MISSING_OR_INVALID");
        }

        int integer(const cv::FileNode &n, const char *key)
        {
            return config_value::integer(config_value::required(n, key, bad), bad);
        }

        double number(const cv::FileNode &n, const char *key)
        {
            return config_value::number(config_value::required(n, key, bad),
                                        config_value::NumericPolicy::NodeNumber, bad);
        }

        std::string text(const cv::FileNode &n, const char *key)
        {
            return config_value::text(config_value::required(n, key, bad), bad);
        }

    }

    // 读取detector.yaml配置文件，返回AppConfig结构体
    AppConfig loadConfig(const std::filesystem::path &path)
    {
        cv::FileStorage fs(path.string(), cv::FileStorage::READ);
        if (!fs.isOpened())
            throw ConfigError("CONFIG_OPEN_FAILED:" + path.string());

        fields(fs.root(), {"schema_version", "preprocess", "geometry", "detector", "output"});
        if (integer(fs.root(), "schema_version") != 2)
            throw ConfigError("CONFIG_SCHEMA_UNSUPPORTED");
        fields(fs["preprocess"], {"work_width", "work_height", "threshold"});
        fields(fs["geometry"], {"white_threshold", "approximation_epsilon"});
        fields(fs["detector"], {"corner"});
        fields(fs["detector"]["corner"], {"physical_to_screen"});
        fields(fs["output"], {"run_directory", "video_fourcc", "video_filename"});

        AppConfig a;
        // 覆盖/检查
        auto &d = a.detector_config;
        d.preprocess = {integer(fs["preprocess"], "work_width"),
                        integer(fs["preprocess"], "work_height"),
                        integer(fs["preprocess"], "threshold")};
        d.geometry_ = {integer(fs["geometry"], "white_threshold"),
                       number(fs["geometry"], "approximation_epsilon")};
        auto m = fs["detector"]["corner"]["physical_to_screen"];
        if (!m.isSeq() || m.size() != 4)
            throw ConfigError("CONFIG_FIXED_MAPPING");
        for (int i = 0; i < 4; ++i)
            d.corner_.physical_to_screen_[i] = config_value::integer(m[i], bad);
        if (!fs["output"]["run_directory"].empty())
            a.offline.directory = text(fs["output"], "run_directory");
        a.offline.video_fourcc = text(fs["output"], "video_fourcc");
        a.offline.video_filename = text(fs["output"], "video_filename");

        // 校验
        validateDetectorConfig(d);
        validateOfflineConfig(a);
        return a;
    }

    void validateDetectorConfig(const DetectorConfig &d)
    {
        // 借排序来验证physical_to_screen_是否为0,1,2,3的排列组合（复制）
        auto m = d.corner_.physical_to_screen_;
        std::sort(m.begin(), m.end());
        
        if (d.preprocess.work_width <= 0 || d.preprocess.work_height <= 0 ||
            d.preprocess.threshold < 0 || d.preprocess.threshold > 255 ||
            d.geometry_.white_threshold_ < 0 || d.geometry_.white_threshold_ > 255 ||
            !std::isfinite(d.geometry_.approximation_epsilon_) ||
            d.geometry_.approximation_epsilon_ < 0 || m != std::array<int, 4>{{0, 1, 2, 3}})
            throw ConfigError("CONFIG_DIMENSION_THRESHOLD_OR_MAPPING_INVALID");
    }

    void validateOfflineConfig(const AppConfig &a)
    {
        if (a.offline.directory.empty() || a.offline.video_fourcc != "MJPG" ||
            a.offline.video_filename != "overlay.avi")
            throw ConfigError("CONFIG_OUTPUT_INVALID");
    }

}
