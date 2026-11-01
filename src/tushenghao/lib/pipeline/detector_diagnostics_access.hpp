#pragma once
#include "mark/detector.hpp"
#include <exception>
#include "pipeline/diagnostics_context.hpp"
namespace mark {
// 内部friend通道代替新增公共observer；take仅移动诊断载荷，不能改变算法历史。
struct RecordedExecution {
 FrameResult result;
 FrameRecord record;
 std::exception_ptr failure;
 std::chrono::nanoseconds bookkeeping{};
};
// A05→A06：异常也移出本次记录，调用方先保存失败证据再重新抛出。
RecordedExecution execute_and_record(Detector&, const FrameInput&, const DiagnosticsRequest&);
class DetectorDiagnosticsAccess {
public:
 static void prepare(Detector&,const DiagnosticsRequest&);
 static FrameRecord take(Detector&);
};
}
