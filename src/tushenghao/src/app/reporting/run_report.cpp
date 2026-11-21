// 汇总帧数、检测、位姿、跟踪和原因计数，写入本次运行的 summary.yaml。
// 写 summary.yaml 的地方
#include "reporting/run_report.hpp"
#include <utility>

namespace mark
{
    void RunSummary::add(const detector::FrameResult &frame)
    {
        ++frames_; // 总帧数 +1；每帧调一次 add，最后 frames_ 就是总帧数
        detections_ += frame.detections_.size(); // 把这帧的检测个数加到总数上（每帧一般是 0 或 1）
        // 一个静态（建一次，可复用）字符串数组，把状态枚举（0、1、2）翻成名字；顺序和 Status 枚举一致（INVALID_INPUT=0、NOT_DETECTED=1、DETECTED=2）
        static const char *names[]{"INVALID_INPUT", "NOT_DETECTED", "DETECTED"};
        // static_cast<size_t>(...) 把枚举（常量，没引号）显式转成 size_t（数组下标要用的类型）
        ++statuses_[names[static_cast<size_t>(frame.status_)]];
        // 遍历这帧的诊断原因（vector），每个原因字符串在 reasons_ 里计数 +1（++map[key]）
        for (const auto &reason_ : frame.diagnostics_)
            ++reasons_[reason_];
    }

    // 自由函数的定义
    void write_run_summary(const RunSummary &s, const std::string &path)
    {
        // 以写模式打开 FileStorage，创建/覆盖 summary.yaml；加载器用的是 READ（读），这里是 WRITE（写）
        cv::FileStorage fs(path, cv::FileStorage::WRITE);
        // 写 frames 和 detections；<< 是 FileStorage 的流式写入（和读 >> 是反方向）；std::to_string 把数字转成字符串再写
        fs << "frames" << std::to_string(s.frames_) << "detections"
           << std::to_string(s.detections_);
        // 一个 for 循环，遍历一个花括号列表里的 4 个 pair；每个 pair 是（名字，map 的指针）
        // std::make_pair() 造一个 pair：first 是 YAML 里的 key 名，second 是 map 的指针，取地址；用指针是因为 pair 里放引用不方便，放指针再 * 解引用
        // 第一个用 make_pair 把该列表的元素类型定下来，后面的花括号就按 pair 理解（简写）
        // 目的：四个 map 用同一段代码写
        for (const auto &entry : {std::make_pair("statuses", &s.statuses_),
                                  {"reasons", &s.reasons_},
                                  {"poses", &s.poses_},
                                  {"tracks", &s.tracks_}})
        {
            // 写 key，再写 "["
            // 在 FileStorage 的流式语法里，"[" 表示“开始一个序列（列表）”，后面要配一个 "]" 关上（之间为列表项）
            fs << entry.first << "[";
            // 遍历这个 map 里的每一项
            // *entry.second 把指针解引用，拿到 map 本身；
            // const auto &[name, count] 是 C++17 结构化绑定([] 是“拆开”的语法)：把 map 里的每一项（pair,一个 key-value 对）直接拆成两个变量 name（key，string）和 count（value，uint64_t）
            for (const auto &[name, count] : *entry.second)
                fs << "{" << "name" << name << "count" << std::to_string(count) << "}"; // 把一项写成 YAML 里的一个 {name, count} （映射）对象（"{" 开一个映射，之间为键值对（"frames": 值），"}"关上。）
            fs << "]";  // "]" 关上这个序列（列表）
            // 举例：一个 map 写出来是：statuses: [ {name: "DETECTED", count: "5"}, ... ]
        }
        // 写总耗时 elapsed_s（秒），直接写 double（YAML 里是数字，没转字符串）
        fs << "elapsed_s" << s.elapsed_s_;
    }

}
// OpenCV FileStorage 的写入 API: <<
// 用 << 描述结构（key、值、列表、对象），FileStorage 把它变成 YAML 文件
// 格式（换行、缩进）由 FileStorage 的写入器决定的：每个键值对占一行，列表项带 - 缩进。文件后缀（.yaml）决定输出是 YAML
/* 结构示例：
frames: "1676"          # 顶层，第 1 个键值对
detections: "1050"      # 顶层，第 2 个
statuses:               # 顶层，第 3 个键，值是一个列表
   - { name: "DETECTED", count: "1050" }   # 列表里的元素，缩进一级
   - { name: "NOT_DETECTED", count: "626" } # 每项是个 {} map，一行内的流式，用逗号分开，不像顶层那样一行一个（OpenCV 写 {} 的格式）
elapsed_s: 12.34       # 顶层，第 4 个
*/
// FileStorage << 规则：
// 在 map 里，<< 按“键、值、键、值”交替：键后面必跟值，值后面必跟下一个键。
// 一个字符串是键还是值，看位置不看内容。
// "[" 开列表（里面只写元素，不分键值），"]" 关列表；
// "{" 开嵌套 map（恢复键值交替），"}" 关 map。
// ！！！！！！！！
