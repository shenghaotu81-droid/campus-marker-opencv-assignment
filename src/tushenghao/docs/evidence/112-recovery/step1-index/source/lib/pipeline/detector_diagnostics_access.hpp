#pragma once
#include "mark/detector.hpp"
#include "pipeline/diagnostics_context.hpp"
namespace mark {
// 内部friend通道代替新增公共observer；take仅移动诊断载荷，不能改变算法历史。
class DetectorDiagnosticsAccess {
public:
 static void prepare(Detector&,const DiagnosticsRequest&);
 static FrameRecord take(Detector&);
};
}
