#pragma once
#include <cstdint>
#include <optional>
#include <set>
#include <string>
namespace mark {
// 旧 EOF 判定无法区分容器截断与完整结束；读取数和提交数必须各自核验。
struct InputCompletion {
    bool incomplete{false};
    bool coverage_verified{false};
    std::uint64_t frames_read{0};
    std::optional<std::uint64_t> expected_frames;
    std::string reason;
};
std::optional<std::uint64_t> metadata_frame_count(double value);
InputCompletion evaluate_input_completion(std::uint64_t frames_read,
                                          std::optional<std::uint64_t> expected_frames, bool user_stopped,
                                          bool explicit_subset,
                                          const std::set<std::uint64_t> &pending_subset);
} // namespace mark
