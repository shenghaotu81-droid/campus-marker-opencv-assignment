// 叠加视频的输出器（VideoExporter），负责把调试叠加视频（overlay.avi）一帧一帧写出来
#pragma once
#include <filesystem>
#include <opencv2/videoio.hpp>

namespace mark
{
    // 逐帧写 MJPG/AVI；完成时关闭编码器并将临时文件发布为指定文件名。（逐帧写，最后完成时才把临时文件变成正式文件）
    class VideoExporter
    {
      public:
        // 三个方法都不是 const，会改它自己的状态；没有状态要藏、不要独占；简单的值类型
        // 按目标文件名、帧尺寸、帧率打开输出器；主线里在帧循环第一帧（id==0）且 overlay 开着时调一次
        void open(const std::filesystem::path &filename, cv::Size, double fps);
        // 写一帧叠加图；主线里每帧调一次
        void write(const cv::Mat &overlay);
        // 完成：关闭编码器，把临时文件发布成正式文件；主线里循环结束后调一次
        void finish();

      private:
        // OpenCV 的视频写入器（真正的编码器），往临时文件里写帧
        // 是 OpenCV “一帧一帧写视频文件”的类，和 VideoCapture 正好是一对
        // VideoCapture 是读视频（capture.read(image_) 一帧一帧读出来），VideoWriter 是写视频（writer_.open(...) 打开、writer_.write(overlay) 一帧一帧写进去）
        // 用法：open 时给文件名、后端（这里是 CAP_FFMPEG，就是用 FFmpeg 库来编码）、编码格式（fourcc，这里是 'M','J','P','G' = MJPG，Motion JPEG，每帧是一张 JPEG 图）、帧率、帧尺寸；然后每帧调一次 write 传一张图；最后 release 关掉编码器、收尾文件
        cv::VideoWriter writer_;
        // 写在一行的两个 path：partial_ 是临时文件（比如 "overlay.partial.avi"），filename_ 是最终目标文件（"overlay.avi"）
        // 临时文件和目标在同一目录，这样最后的改名是原子的（要么一下子完成（"overlay.avi" 瞬间出现，而且是完整的），要么完全没发生（"overlay.avi" 不存在））
        // open 时写的是临时文件，不是最终文件；finish 时先关编码器，再把临时文件改名成最终文件
        // 程序中途崩了，最终文件也不会留下一半写坏的（临时文件里是未完成的数据，最终文件只有跑完才出现）
        std::filesystem::path partial_, filename_;
    };
}
