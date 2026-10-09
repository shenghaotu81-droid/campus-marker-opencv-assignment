#include "run_console.hpp"
#include <iomanip>
#include <map>
#include <ostream>

namespace mark
{
    // 原状态数值不便人工核对；标签只作展示，技术状态字段保持原值。
    std::string status_label(Status status)
    {
        switch (status)
        {
        case Status::NOT_READY:
            return "尚未就绪";
        case Status::INVALID_INPUT:
            return "输入非法";
        case Status::NOT_DETECTED:
            return "未检测";
        case Status::DETECTED:
            return "检测";
        }
        return "未映射状态";
    }

    // 原错误仅技术码；显式映射已知类别，未知值保留原码而不猜测原因。
    std::string reason_label(const std::string &reason)
    {
        static const std::map<std::string, std::string> labels{
            {"COMPLETE", "完整"},
            {"EXPLICIT_SUBSET", "显式子集完成"},
            {"USER_STOP", "用户主动停止"},
            {"FRAME_COUNT_MISMATCH", "实际读取帧数与期待数不一致"},
            {"SUBSET_MISSING", "请求的子集帧缺失"},
            {"COVERAGE_UNVERIFIED", "完整性未证实；请提供 --expected-frames"},
            {"NO_DISPLAY", "没有可用显示环境"},
            {"PROBE_MISSING", "显示探测程序缺失"},
            {"PROBE_FAILED", "显示探测失败"},
            {"PROBE_TIMEOUT", "显示探测超时"},
            {"PROBE_FORK_FAILED", "无法启动显示探测"},
            {"PROBE_WAIT_FAILED", "无法回收显示探测"},
            {"WINDOW_OPEN_FAILED", "窗口打开失败"},
            {"DISPLAY_RUNTIME_EXCEPTION", "显示运行异常"}};
        auto found = labels.find(reason);
        if (found != labels.end())
            return found->second;
        // 所有已有诊断枚举都有中文类别，技术名称仍原样输出。
        const std::pair<ReasonCode, const char *> codes[]{
            {ReasonCode::InputFormat, "图像格式非法"},
            {ReasonCode::InvalidStamp, "时间戳非法"},
            {ReasonCode::InvalidSequence, "帧序非法"},
            {ReasonCode::BudgetMissing, "批准预算缺失"},
            {ReasonCode::ClearlyIncomplete, "目标明确不完整"},
            {ReasonCode::AssignmentInsufficient, "结构对应证据不足"},
            {ReasonCode::CornerRejected, "角点证据被拒绝"},
            {ReasonCode::ScreenRejected, "屏幕排序被拒绝"},
            {ReasonCode::ValidationRejected, "几何有效性检查拒绝"},
            {ReasonCode::UnresolvedCompetition, "竞争解释未排除"},
            {ReasonCode::SemanticRejected, "语义解释被拒绝"},
            {ReasonCode::PublishFloatRejected, "公开浮点表示被拒绝"},
            {ReasonCode::HistoryExpired, "选择历史过期"},
            {ReasonCode::AssociationFailed, "关联失败"},
            {ReasonCode::AssociationAmbiguous, "关联有歧义"},
            {ReasonCode::CorrespondenceAmbiguous, "四点对应有歧义"},
            {ReasonCode::ZeroDt, "时间间隔为零"},
            {ReasonCode::SmoothingRejected, "平滑结果被拒绝"},
            {ReasonCode::ResetExternal, "外部重置"},
            {ReasonCode::ResetInputChanged, "输入源变更重置"},
            {ReasonCode::ResetInvalidSequence, "非法序列重置"},
            {ReasonCode::NonFiniteField, "非有限字段"},
            {ReasonCode::IoFailure, "输入输出失败"},
            {ReasonCode::OtherRecordedReason, "其他已记录原因"}};
        for (const auto &code : codes)
            if (reason == reasonName(code.first))
                return code.second;
        return "未映射原因";
    }

    namespace
    {
        // 原性能摘要缺算法瓶颈口径；只选四个实际有测量样本的算法阶段，不重复比较 total。
        void write_timing_console(std::ostream &out, const RunSummary &summary, bool enabled)
        {
            if (!enabled)
            {
                out << "阶段计时未启用；瓶颈阶段不可判定\n";
                return;
            }
            auto total = summarizeDurations(summary.durations[size_t(Stage::ProcessTotal)]);
            if (total.n)
                out << "耗时：处理平均 " << *total.mean_ns / 1e6 << " ms，p95 "
                    << *total.p95_ns / 1e6 << " ms；";
            else
                out << "耗时：当前范围无公共处理总计样本；";
            out << "运行总耗时 " << double(summary.wall.count()) / 1e9 << " s\n";
            const char *names[]{"预处理", "几何检测", "语义解码", "输出稳定"};
            std::optional<double> maximum;
            size_t chosen = 0;
            for (size_t stage = 1; stage <= 4; ++stage)
            {
                auto measured = summarizeDurations(summary.durations[stage]);
                if (measured.mean_ns && (!maximum || *measured.mean_ns > *maximum))
                {
                    maximum = measured.mean_ns;
                    chosen = stage;
                }
            }
            if (maximum)
                out << "瓶颈阶段（已测样本平均值）：" << names[chosen - 1] << ' ' << *maximum / 1e6
                    << " ms\n";
            else
                out << "瓶颈阶段不可判定：无已测算法阶段样本\n";
        }
    } // namespace

    // 原英文技术行被脚本消费；保留格式，之后追加中文数量、范围、真实统计与原因标签。
    void write_run_console(std::ostream &out, const RunMetadata &metadata,
                           const RunSummary &summary, uint64_t frames_read, bool timing_enabled,
                           ExecutionScope scope)
    {
        out << "run=" << metadata.run_id << " frames=" << summary.submitted
            << " fingerprint=" << summary.result_fingerprint << " incomplete=" << summary.incomplete
            << '\n';
        out << "运行结果："
            << (summary.failed       ? "失败"
                : summary.incomplete ? "不完整"
                                     : "完整")
            << '\n';
        auto count = [&](Status status)
        {
            auto it = summary.statuses.find(std::to_string(int(status)));
            return it == summary.statuses.end() ? uint64_t{} : it->second;
        };
        out << "帧数：读取 " << frames_read << "，处理 " << summary.submitted << "；"
            << status_label(Status::DETECTED) << ' ' << count(Status::DETECTED) << "，"
            << status_label(Status::NOT_DETECTED) << ' ' << count(Status::NOT_DETECTED) << '\n';
        out << "执行范围：" << scopeName(scope) << "；覆盖：" << metadata.environment.at("coverage")
            << '\n';
        const auto &reason = metadata.environment.at("completion_reason");
        out << "完整性：" << reason_label(reason) << "（" << reason << "）\n";
        out << std::fixed << std::setprecision(3);
        write_timing_console(out, summary, timing_enabled);
    }

    // 零帧/打开失败没有有效 summary；只展示失败说明，保留调用方原异常详情。
    void write_failure_console(std::ostream &out, const std::string &detail)
    {
        out << "运行结果：失败；";
        // 原子串猜测会把未知且含 video 的错误当作输入失败；仅映射明确的已有异常。
        if (detail.rfind("Config error", 0) == 0)
            out << "配置校验失败";
        else if (detail.rfind("VIDEO_EXPORT", 0) == 0)
            out << "视频导出失败";
        else if (detail == "cannot open video" || detail == "invalid video fps" ||
                 detail == "video timestamp overflow")
            out << "视频输入失败";
        else if (detail == "zero-frame run")
            out << "没有读取到有效帧";
        else
            out << "未映射原因";
        out << "（" << detail << "）\n";
    }
} // namespace mark
