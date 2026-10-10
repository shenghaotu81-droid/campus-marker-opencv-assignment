#pragma once
#include "config/config.hpp"
#include "pipeline/detector_diagnostics_access.hpp"
#include "block3_fixture.hpp"
#include "temporal_fixture.hpp"
#include <filesystem>

namespace observability_fixture
{
    using temporal_fixture::check;

    // 显式使用已批准生产配置，合成图像只作主动行为fixture，不反推门限。
    inline mark::AppConfig app()
    {
        return mark::loadConfig(std::filesystem::path(__FILE__).parent_path().parent_path() /
                                "config/detector.yaml");
    }

    inline mark::FrameInput frame(uint64_t id = 0, int64_t time = 0, bool target = false)
    {
        return {target ? fixture::scene().frame.original_image_
                       : cv::Mat(1080, 1440, CV_8UC3, cv::Scalar(0, 0, 0)),
                id, time, mark::TimestampSource::Unknown};
    }

    inline mark::DiagnosticsRequest request(bool timing = true, bool selected = true)
    {
        mark::DiagnosticsRequest r;
        r.run_id = "fixture";
        r.timing_enabled = timing;
        r.selected = selected;
        r.level = mark::DiagnosticsLevel::Evidence;
        return r;
    }

    template <class F> inline void rejects(F f)
    {
        bool failed = false;
        try
        {
            f();
        }
        catch (const std::exception &)
        {
            failed = true;
        }
        check(failed, "expected exception");
    }
}
