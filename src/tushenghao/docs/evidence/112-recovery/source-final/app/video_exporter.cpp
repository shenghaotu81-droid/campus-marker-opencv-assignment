#include "video_exporter.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>
namespace mark {
// 原导出无真实编码器约束；固定 FFmpeg+MJPG，打开失败明确拒绝，不改编码或容器。
void VideoExporter::open(const std::filesystem::path &run_dir, cv::Size size, double fps,
                         const std::string &fourcc) {
    if (!partial_.empty() || size.width <= 0 || size.height <= 0 || !std::isfinite(fps) || fps <= 0 ||
        fourcc != "MJPG")
        throw std::runtime_error("VIDEO_EXPORT_INVALID_OPEN");
    partial_ = run_dir / "overlay.partial.mp4";
    size_ = size;
    fps_ = fps;
    if (std::filesystem::exists(partial_) || std::filesystem::exists(run_dir / "overlay.mp4"))
        throw std::runtime_error("VIDEO_EXPORT_PATH_EXISTS");
    if (!writer_.open(partial_.string(), cv::CAP_FFMPEG, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), fps,
                      size))
        throw std::runtime_error("VIDEO_EXPORT_MJPG_MP4_UNSUPPORTED");
    backend_ = writer_.getBackendName();
}
// 原来没有逐帧写出验证；只写当前帧合法 BGR8 overlay，返回 void 不能当作成功落盘证明。
void VideoExporter::write(const cv::Mat &overlay) {
    if (!writer_.isOpened() || finished_ || overlay.empty() || overlay.type() != CV_8UC3 ||
        overlay.size() != size_)
        throw std::runtime_error("VIDEO_EXPORT_INVALID_FRAME");
    writer_.write(overlay);
    ++written_frames_;
}
// 原文件存在无法证明可播放；release 后逐帧读回，数量/尺寸/源帧周期一致才允许发布。
void VideoExporter::finish(bool input_complete) {
    if (partial_.empty() || finished_)
        throw std::runtime_error("VIDEO_EXPORT_FINISH_SEQUENCE");
    writer_.release();
    finished_ = true;
    cv::VideoCapture reader(partial_.string(), cv::CAP_FFMPEG);
    if (!reader.isOpened())
        throw std::runtime_error("VIDEO_EXPORT_READBACK_OPEN_FAILED");
    readback_fps_ = reader.get(cv::CAP_PROP_FPS);
    // MP4 时间基的表示允许半个微秒帧周期舍入；不引入检测容差。
    if (!std::isfinite(readback_fps_) || readback_fps_ <= 0 ||
        std::llround(1e6 / fps_) != std::llround(1e6 / readback_fps_))
        throw std::runtime_error("VIDEO_EXPORT_SOURCE_PERIOD_MISMATCH");
    std::uint64_t count = 0;
    cv::Mat frame;
    while (reader.read(frame)) {
        if (frame.empty() || frame.size() != size_)
            throw std::runtime_error("VIDEO_EXPORT_READBACK_SIZE_MISMATCH");
        ++count;
    }
    reader.release();
    if (!count || count != written_frames_)
        throw std::runtime_error("VIDEO_EXPORT_READBACK_COUNT_MISMATCH");
    if (input_complete)
        std::filesystem::rename(partial_, partial_.parent_path() / "overlay.mp4");
}
} // namespace mark
