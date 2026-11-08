#include "preprocess/preprocess.hpp"
#include <opencv2/imgproc.hpp>

namespace mark::detector
{
    // 保存原图只读视图和帧上下文，生成工作图及像素中心坐标映射供结构匹配使用。
    PreparedFrame preprocess(const FrameInput &frame, const PreprocessConfig &config)
    {
        PreparedFrame prepared;
        prepared.original_image_ = frame.image_;
        prepared.original_white_threshold_ = config.threshold_;
        prepared.frame_id_ = frame.frame_id_;
        prepared.timestamp_us_ = frame.timestamp_us_;
        cv::resize(frame.image_, prepared.image_, {config.work_width_, config.work_height_});
        prepared.scale_x_ = static_cast<double>(config.work_width_) / frame.image_.cols;
        prepared.scale_y_ = static_cast<double>(config.work_height_) / frame.image_.rows;
        return prepared;
    }
}
