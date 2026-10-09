#pragma once
#include "pipeline/run_report.hpp"
#include <iosfwd>
namespace mark {
// 原终端只给技术行；中文展示仅消费现有统计，不进入检测和公开结果。
void write_run_console(std::ostream &out, const RunMetadata &metadata, const RunSummary &summary,
                       std::uint64_t frames_read, bool timing_enabled, ExecutionScope scope);
void write_failure_console(std::ostream &out, const std::string &detail);
std::string reason_label(const std::string &reason);
std::string status_label(Status status);
} // namespace mark
