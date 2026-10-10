#pragma once
#include <chrono>
#include <filesystem>
#include <opencv2/core.hpp>
#include <string>

namespace mark
{
    // 旧 App 在无效 DISPLAY 上直接启动 Qt 会 abort；先由隔离进程探测，再按可用状态显示。
    class DisplaySession
    {
      public:
        ~DisplaySession();
        bool start(bool requested, const std::filesystem::path &probe_path,
                   std::chrono::milliseconds timeout);
        bool show_and_poll(const cv::Mat &overlay);

        bool active() const
        {
            return active_;
        }

        const std::string &unavailable_reason() const
        {
            return reason_;
        }

        std::chrono::nanoseconds visualize_elapsed() const
        {
            return visualize_elapsed_;
        }

        std::chrono::nanoseconds wait_elapsed() const
        {
            return wait_elapsed_;
        }

      private:
        bool active_{false};
        std::string reason_{"NOT_REQUESTED"};
        std::chrono::nanoseconds visualize_elapsed_{}, wait_elapsed_{};
    };

    std::filesystem::path display_probe_path();
} // namespace mark
