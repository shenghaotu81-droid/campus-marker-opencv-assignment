#pragma once
#include <cstdint>
#include <filesystem>
#include <opencv2/videoio.hpp>

namespace mark
{
    // 原视频导出只有未实现开关；用 partial 生命周期，实际读回后才发布完整文件。
    class VideoExporter
    {
      public:
        void open(const std::filesystem::path &run_dir, cv::Size original_size, double source_fps,
                  const std::string &fourcc);
        void write(const cv::Mat &overlay);
        void finish(bool input_complete);

        std::uint64_t written_frames() const
        {
            return written_frames_;
        }

        bool opened() const
        {
            return writer_.isOpened();
        }

        const std::string &backend() const
        {
            return backend_;
        }

        double readback_fps() const
        {
            return readback_fps_;
        }

      private:
        cv::VideoWriter writer_;
        std::filesystem::path partial_;
        cv::Size size_;
        double fps_{}, readback_fps_{};
        std::uint64_t written_frames_{};
        bool finished_{false};
        std::string backend_;
    };
} // namespace mark
