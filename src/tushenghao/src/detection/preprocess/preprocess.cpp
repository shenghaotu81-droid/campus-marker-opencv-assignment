// 保留原图浅视图，按配置生成工作图。
#include "preprocess/preprocess.hpp"
#include <opencv2/imgproc.hpp>

namespace mark::detector
{
    PreparedFrame preprocess(const FrameInput &frame, const PreprocessConfig &config)
    {
        PreparedFrame prepared;
        prepared.original_image_ = frame.image_;

        // 这个阈值用于原图角片。工作图分割使用单独的几何阈值。
        prepared.original_white_threshold_ = config.threshold_;

        cv::resize(frame.image_, prepared.image_, {config.work_width_, config.work_height_});

        // 记录工作图与原图的尺寸比，供像素中心换算。
        prepared.scale_x_ = static_cast<double>(config.work_width_) / frame.image_.cols;
        prepared.scale_y_ = static_cast<double>(config.work_height_) / frame.image_.rows;

        return prepared;
    }
}
