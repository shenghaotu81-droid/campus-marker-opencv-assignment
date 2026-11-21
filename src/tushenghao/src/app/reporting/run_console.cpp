// 控制台打印总结
#include "reporting/run_console.hpp"
#include <ostream>

namespace mark
{
    // 把 RunSummary 里的汇总打印到控制台（out 是 std::cout）——一行总览（帧数、目标数、用时），然后位姿状态计数、跟踪状态计数各打几行
    /* std::cout参数：
    std::cout 是个全局对象（类型 std::ostream，<iostream> 提供），代表标准输出（终端）。
    write_run_console(std::ostream &out, ...) 的参数是引用。调用时 out 绑定到 std::cout 上——是同一个对象，不是拷贝。
    流不可拷贝（拷贝构造被删了），所以只能传引用，不能传值。
    好处是通用：传 std::cout 打终端，传 std::ofstream 写文件，传 std::ostringstream 拼字符串——ofstream/ostringstream 都继承自 ostream，函数里只管 <<，不用管输出去哪。
    */
    void write_run_console(std::ostream &out, const RunSummary &s)
    {
        // 总览行：
        // 例如拼出来的就是：已处理 1676 帧, 检测 1676 个目标, 用时 2.5 秒，然后换行。'\n' 是换行符（和 std::endl 不一样，'\n' 只换行不刷流）
        out << "已处理 " << s.frames_ << " 帧，检测 " << s.detections_ << " 个目标";
        out << "，用时 " << s.elapsed_s_ << " 秒";
        out << '\n';
        // 位姿计数：
        // for (const auto &[name, count] : s.poses_) 是 C++17 的结构化绑定：s.poses_ 是个 map（状态名 → 次数），循环里每个元素是个 pair，[name, count] 直接把它拆成名字和次数两个变量（作用是把 map 里的每个 pair 直接拆成名字和次数两个变量）
        // 比如打出来就是：
        // 位姿 ESTIMATED: 1088
        // 位姿 UNAVAILABLE: 588
        for (const auto &[name, count] : s.poses_)
            out << "位姿 " << name << ": " << count << '\n';
        // 跟踪计数：
        // 同上套路，s.tracks_ 是跟踪状态的计数 map，打出来比如：
        // 跟踪 MEASURED: 1675
        // 跟踪 COASTING: 1
        for (const auto &[name, count] : s.tracks_)
            out << "跟踪 " << name << ": " << count << '\n';
        // 注释：RunSummary 里其实有 4 个计数 map（statuses_、reasons_、poses_、tracks_），但控制台只打印了 poses_ 和 tracks_ 这两个——事件原因（reasons_，比如 TRACK_CREATED 出现了几次）只写进 summary.yaml，不打到控制台上
    }

}
