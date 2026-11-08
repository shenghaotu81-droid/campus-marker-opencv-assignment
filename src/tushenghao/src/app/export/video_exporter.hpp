#pragma once
#include <filesystem>
#include <opencv2/videoio.hpp>

namespace mark
{
    // 逐帧写 MJPG/AVI；完成时关闭编码器并将临时文件发布为指定文件名。
    class VideoExporter
    {
      public:
        void open(const std::filesystem::path &filename, cv::Size, double fps);
        void write(const cv::Mat &overlay);
        void finish();
      private:
        cv::VideoWriter writer_;
        std::filesystem::path partial_, filename_;
    };
}
