// 用 MJPG 编码同目录临时 AVI，再发布到指定路径。
#include "export/video_exporter.hpp"

namespace mark
{
    void VideoExporter::open(const std::filesystem::path &filename, cv::Size size, double fps)
    {
        filename_ = filename;

        partial_ = filename.parent_path() / (filename.stem().string() + ".partial.avi");

        writer_.open(partial_.string(), cv::CAP_FFMPEG, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
                     fps, size);
    }

    void VideoExporter::write(const cv::Mat &overlay)
    {
        writer_.write(overlay);
    }

    void VideoExporter::finish()
    {
        // 关编码器后再改名，完整性由回放验收。
        writer_.release();

        std::filesystem::rename(partial_, filename_);
    }
}
