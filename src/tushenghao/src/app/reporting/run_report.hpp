// 运行汇总（统计）的文件：RunSummary 在整趟运行里累计计数，最后写成 summary.yaml
#pragma once
#include "vision/types/detector_types.hpp"
#include <map>

namespace mark
{
    struct RunSummary
    {
        // 写在一行的两个计数，都是 {} 默认 0
        // frames_ 是总帧数，detections_ 是总检测数
        // uint64_t 是无符号 64 位整数（计数不会是负数，64 位不会溢出）
        uint64_t frames_{}, detections_{};
        // 写在一行的四个计数 map（字典：名字 → 次数）
        // statuses_——按检测状态计（"INVALID_INPUT" / "NOT_DETECTED" / "DETECTED"）
        // reasons_——按诊断原因字符串计（FrameResult.diagnostics_ 里的）
        // poses_——按位姿状态计（"ESTIMATED" / "UNAVAILABLE"，帧循环里 ++summary.poses_[pose_status_name(p)] 填）
        // tracks_——按轨迹状态计（"MEASURED" / "COASTING"，帧循环里 ++summary.tracks_[track_state_name(t.state_)] 填）
        // 用 map 是因为名字是任意的（出现什么名字就计什么），不用事先定好;
        // ++map[名字(key)] 的计数写法：名字第一次出现时 map 自动建个 0 再加 1，不用先判断在不在
        std::map<std::string, uint64_t> statuses_, reasons_, poses_, tracks_;
        // 总耗时（秒），默认 0，循环结束后才填（Clock::now() - start）
        double elapsed_s_{};
        // 成员函数，把一帧的检测结果累计进汇总（帧数 +1、检测数加上这帧的个数、状态计数 +1、诊断原因各 +1）
        // 声明在这里，定义在 run_report.cpp，帧循环里每帧调一次（summary.add(raw);）
        void add(const detector::FrameResult &frame);
    };

    // 自由函数，把汇总写到指定路径的 summary.yaml 里
    // 声明在这里，定义在 run_report.cpp（开 FileStorage 写 frames/detections、四个 map 各写成 {name, count} 的列表、再写 elapsed_s）
    // offline_runner 循环结束后调它（write_run_summary(summary, (directory / "summary.yaml").string());）
    void write_run_summary(const RunSummary &summary, const std::string &path);
}
