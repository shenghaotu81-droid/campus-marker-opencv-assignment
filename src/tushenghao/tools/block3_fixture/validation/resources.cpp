// 输入固定真实轮廓fixture及显式资源策略；输出竞争数量、截断、耗时和峰值RSS。
// 只测M/S补全资源，不以正例平均数或3sigma替代资源策略，不修改生产配置。
#include "block3_fixture.hpp"
#include "geometry/completion/geometry_assignment_completion.hpp"
#include "pipeline/decode/decode_stage.hpp"
#include <chrono>
#include <sys/resource.h>
#include <iostream>

int main()
{
    const mark::AssignmentCompletionConfig policy{3, 1, 10, 0.08, 1, 128, 4096, 64};
    for (auto scenario : std::vector<std::pair<int, int>>{
             {0, 1}, {1, 1}, {4, 1}, {16, 1}, {64, 1}, {256, 1}, {0, 1000}, {16, 1000}})
    {
        int duplicates = scenario.first, parents = scenario.second;
        auto scene = fixture::scene();
        size_t next_id = 1000;
        for (const auto &a : scene.hypothesis.assignments_)
            if (a.model_part_id_[0] != 'L')
            {
                mark::WhiteComponent source;
                for (const auto &c : scene.frame.components_)
                    if (c.component_id_ == a.component_id_)
                        source = c;
                for (int i = 0; i < duplicates; ++i)
                {
                    auto c = source;
                    c.component_id_ = next_id++;
                    scene.frame.components_.push_back(c);
                }
            }
        mark::GeometryBatch input;
        input.hypotheses_.push_back(scene.hypothesis);
        input.hypotheses_[0].assignments_.resize(3);
        input.hypotheses_.resize(parents, input.hypotheses_[0]);
        auto begin = std::chrono::steady_clock::now();
        auto output = mark::completeSegmentedAssignments(input, scene.frame.components_,
                                                         scene.geometry, policy);
        double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin)
                .count();
        auto stage_begin = std::chrono::steady_clock::now();
        auto stage =
            mark::decodeStage(scene.frame, output, scene.geometry, fixture::cornerConfig());
        double decode_ms = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - stage_begin)
                               .count();
        bool unique = false;
        for (const auto &d : stage.detections)
            unique = unique || bool(d.attributes.orientation);
        rusage usage{};
        getrusage(RUSAGE_SELF, &usage);
        std::cout
            << "{\"duplicates_per_part\":" << duplicates << ",\"parents\":" << parents
            << ",\"components\":" << scene.frame.components_.size()
            << ",\"policy\":{\"candidates_per_part\":128,\"expansions_total\":4096,\"output_branches_total\":64}"
            << ",\"branches\":" << output.hypotheses_.size()
            << ",\"truncated\":" << (output.resource_truncated_ ? "true" : "false")
            << ",\"assignment_ms\":" << ms << ",\"decode_ms\":" << decode_ms
            << ",\"peak_rss_kib\":" << usage.ru_maxrss
            << ",\"orientation_unique\":" << (unique ? "true" : "false") << ",\"diagnostics\":[";
        for (size_t i = 0; i < output.diagnostics_.size(); ++i)
        {
            if (i)
                std::cout << ',';
            std::cout << '"' << output.diagnostics_[i] << '"';
        }
        std::cout << "]}\n";
        if (output.hypotheses_.size() > 64 || (output.resource_truncated_ && unique))
            return 1;
    }
    return 0;
}
