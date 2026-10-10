#include "input_completion.hpp"
#include <cmath>
#include <limits>
namespace mark {
// 原代码直接使用容器计数；只接受正、有限、整值，避免浮点转整数的未定义行为。
std::optional<std::uint64_t> metadata_frame_count(double value) {
    if (!std::isfinite(value) || value <= 0 || std::floor(value) != value ||
        static_cast<long double>(value) >= std::ldexp(1.0L, 64))
        return std::nullopt;
    return static_cast<std::uint64_t>(value);
}
// 原来 read=false 一律成功；用户停止、子集缺失和全输入数量差异分别保留技术原因。
InputCompletion evaluate_input_completion(std::uint64_t frames_read,
                                          std::optional<std::uint64_t> expected_frames, bool user_stopped,
                                          bool explicit_subset,
                                          const std::set<std::uint64_t> &pending_subset) {
    InputCompletion result;
    result.frames_read = frames_read;
    result.expected_frames = expected_frames;
    if (user_stopped)
        result.reason = "USER_STOP";
    else if (explicit_subset) {
        result.coverage_verified = pending_subset.empty();
        result.reason = pending_subset.empty() ? "EXPLICIT_SUBSET" : "SUBSET_MISSING";
    } else if (!expected_frames)
        result.reason = "COVERAGE_UNVERIFIED";
    else {
        result.coverage_verified = frames_read == *expected_frames;
        result.reason = result.coverage_verified ? "COMPLETE" : "FRAME_COUNT_MISMATCH";
    }
    result.incomplete = !result.coverage_verified;
    return result;
}
} // namespace mark
