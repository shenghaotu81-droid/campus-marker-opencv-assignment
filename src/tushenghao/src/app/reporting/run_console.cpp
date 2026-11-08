// 控制台打印总结
#include "reporting/run_console.hpp"
#include <ostream>

namespace mark
{
    void write_run_console(std::ostream &out, const RunSummary &s)
    {
        out << "已处理 " << s.frames_ << " 帧，检测 " << s.detections_ << " 个目标";
        out << "，用时 " << s.elapsed_s_ << " 秒";
        out << '\n';
        for (const auto &[name, count] : s.poses_)
            out << "位姿 " << name << ": " << count << '\n';
        for (const auto &[name, count] : s.tracks_)
            out << "跟踪 " << name << ": " << count << '\n';
    }

}
