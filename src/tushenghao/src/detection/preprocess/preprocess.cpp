// 预处理过程——输入原始帧，输出处理好的 PreparedFrame
#include "preprocess/preprocess.hpp"

#include <stdexcept>

#include <opencv2/imgproc.hpp>

namespace mark
{

    PreparedFrame preprocess(
        const FrameInput &frame,
        const PreprocessConfig &config)
    {
        if (frame.image.empty() || frame.image.dims != 2 || frame.image.type() != CV_8UC3)
        {
            throw std::invalid_argument(
                "preprocess: expected non-empty BGR8 image");  // 不把格式错误留给 cvtColor 崩溃。
        }

        if (config.work_width <= 0 ||
            config.work_height <= 0)
        {
            throw std::invalid_argument(
                "preprocess: invalid working size");    // 工作尺寸无效
        }

        if (config.threshold < 0 || config.threshold > 255)
            throw std::invalid_argument("preprocess: invalid white threshold");

        PreparedFrame prepared{};
        // 浅引用只在本次调用使用；resize 写入独立临时图，保证用户像素不变。
        prepared.original_image_ = frame.image;
        prepared.original_white_threshold_ = config.threshold;

        /*
         * 1. 保存帧上下文
         */
        prepared.frame_id_ = frame.frame_id;
        prepared.timestamp_us_ = frame.timestamp_us;

        /*
         * 2. resize
         *
         * 原图:
         *   original_width  x original_height
         *
         * 工作图:
         *   work_width x work_height
         */
        cv::Mat resized;

        cv::resize(
            frame.image,
            resized,
            cv::Size(
                config.work_width,
                config.work_height));

        prepared.scale_x_ =
            static_cast<double>(config.work_width) /
            static_cast<double>(frame.image.cols);

        prepared.scale_y_ =
            static_cast<double>(config.work_height) /
            static_cast<double>(frame.image.rows);

        // 工作图保持BGR8，原三L模块继续使用自身阈值，不改变其算法或参数。
        // 原图取证则使用上面显式记录的preprocess.threshold；两个来源都进入审计配置。
        prepared.image_ = resized;

        return prepared;
    }

} // namespace mark
