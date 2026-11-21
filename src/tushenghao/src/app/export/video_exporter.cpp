// open 写临时文件 → 每帧 write 追加（反复） → finish 关编码器 + 原子改名发布
#include "export/video_exporter.hpp"

// app 层的导出工具
namespace mark
{
    // 临时文件与目标同目录（改名），AVI 容器和 MJPG 编码固定（写死非配置）用于离线回放。
    void VideoExporter::open(const std::filesystem::path &filename, cv::Size size, double fps)
    {
        filename_ = filename; // 把目标文件名（比如 "outputs/marker/overlay.avi"）存进成员 filename_
        // 拼临时文件路径
        // parent_path() 是目录部分（"outputs/marker"）
        // stem() 是去掉后缀的文件名（"overlay.avi" → "overlay"），.string() 转成 string
        // + ".partial.avi" 拼成 "overlay.partial.avi"
        // / 把目录和文件名接起来 → "outputs/marker/overlay.partial.avi"
        partial_ = filename.parent_path() / (filename.stem().string() + ".partial.avi");
        // 在临时文件上打开 VideoWriter
        // partial_.string() 转成 string（VideoWriter::open 要的是字符串文件名）
        // cv::CAP_FFMPEG 是后端（用 FFmpeg 库编码）
        // cv::VideoWriter::fourcc('M','J','P','G') 是 fourcc 编码（MJPG，Motion JPEG，每帧一张 JPEG 图）
        // fps 和 size 是帧率和帧尺寸（让输出视频和输入同速、同尺寸）
        writer_.open(partial_.string(), cv::CAP_FFMPEG, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
                     fps, size);
    }

    void VideoExporter::write(const cv::Mat &overlay)
    {
        writer_.write(overlay); // 把一帧叠加图（cv::Mat）交给 VideoWriter写进临时文件。薄包装（临时文件/改名的逻辑在它外面，逐帧写直接委托给 VideoWriter）
    }

    // 编码结束后发布文件，视频完整性由回放验收验证。
    // finish 只负责发布文件，视频到底完不完整由回放验收测试去验证（这段代码自己不检查视频内容）
    void VideoExporter::finish()
    {
        // 关闭编码器（把临时文件收尾、落盘，此时临时文件已经是个完整的 AVI）
        // 把编码器里剩下的帧刷出来、写容器尾巴（AVI 的索引/结尾）、关文件
        // 忘了调 release，VideoWriter 析构时也会自动 release，但 finish 里显式调更清楚，还能控制“在改名之前关”
        writer_.release();
        // 把临时文件改名成最终文件（发布）；同一目录内改名是原子的
        std::filesystem::rename(partial_, filename_);
    }
}

// path 的拆分：parent_path()（目录）、filename()（带后缀的文件名）、stem()（去后缀的文件名）、extension()（后缀）、string()（转字符串）
// 文件操作：create_directories（mkdir -p）、rename（改名/移动）、exists、remove、copy
