#include "export/video_exporter.hpp"

namespace mark
{
    // 临时文件与目标同目录，AVI 容器和 MJPG 编码固定用于离线回放。
    void VideoExporter::open(const std::filesystem::path &filename, cv::Size size, double fps)
    {
        filename_ = filename;
        partial_ = filename.parent_path() / (filename.stem().string() + ".partial.avi");
        writer_.open(partial_.string(), cv::CAP_FFMPEG,
                     cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), fps, size);
    }

    void VideoExporter::write(const cv::Mat &overlay)
    {
        writer_.write(overlay);
    }

    // 编码结束后发布文件，视频完整性由回放验收验证。
    void VideoExporter::finish()
    {
        writer_.release();
        std::filesystem::rename(partial_, filename_);
    }
}
