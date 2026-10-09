// 沙盒取证：可关闭的函数级计时，验证热点；正式方案不保留全局计时状态。
#pragma once
#include <array>
#include <chrono>
#include <cstdlib>
namespace mark::sandbox {
enum Function {Observe,Gray,Threshold,Contours,Mapping,Fit,ArcGeneration,ArcFit,Pairs,Evidence,GeometryGenerate,GeometryValidate,Completion,Count};
inline const char* names[]={"observe","gray","threshold","contours","mapping","fit","arc_generation","arc_fit","pairs","evidence","geometry_generate","geometry_validate","completion"};
struct Sample{long long ns=0;unsigned long long calls=0;};
inline std::array<Sample,Count> samples{};
inline bool profiling(){static bool on=std::getenv("MARK_SANDBOX_PROFILE")!=nullptr;return on;}
inline void reset(){samples={};}
struct Scope{Function f;bool on;std::chrono::steady_clock::time_point start;explicit Scope(Function v):f(v),on(profiling()){if(on)start=std::chrono::steady_clock::now();}~Scope(){if(on){samples[f].ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();++samples[f].calls;}}};
}
