#include "display_session.hpp"
#include <cerrno>
#include <cstdlib>
#include <opencv2/highgui.hpp>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace mark
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        // 原 GUI 环境判断只看指针；空值也表示没有显示服务。
        bool environment_present(const char *key)
        {
            const char *value = std::getenv(key);
            return value && *value;
        }

        // 原 Qt abort 会终止检测主进程；仅探测子进程消费 highgui，超时只回收自己启动的 PID。
        std::string probe_display(const std::filesystem::path &path,
                                  std::chrono::milliseconds timeout)
        {
            if (!std::filesystem::is_regular_file(path))
                return "PROBE_MISSING";
            const auto executable = path.string();
            const auto pid = fork();
            if (pid < 0)
                return "PROBE_FORK_FAILED";
            if (pid == 0)
            {
                const rlimit limit{0, 0};
                setrlimit(RLIMIT_CORE, &limit);
                execl(executable.c_str(), executable.c_str(), static_cast<char *>(nullptr));
                _exit(127);
            }
            const auto deadline = Clock::now() + timeout;
            for (;;)
            {
                int status = 0;
                const auto result = waitpid(pid, &status, WNOHANG);
                if (result == pid)
                    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? "" : "PROBE_FAILED";
                if (result < 0 && errno != EINTR)
                    return "PROBE_WAIT_FAILED";
                if (Clock::now() >= deadline)
                {
                    kill(pid, SIGKILL);
                    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
                    {
                    }
                    return "PROBE_TIMEOUT";
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    } // namespace

    // 原 probe 定位可能依赖 CWD；真实可执行文件的 sibling 同样适用于 audit 和搬迁后的程序。
    std::filesystem::path display_probe_path()
    {
        std::error_code error;
        auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
        return error ? std::filesystem::path{} : executable.parent_path() / "marker_display_probe";
    }

    // 原来请求窗口直接报错或 abort；无屏与失败探测降级，检测和导出仍继续。
    bool DisplaySession::start(bool requested, const std::filesystem::path &probe_path,
                               std::chrono::milliseconds timeout)
    {
        if (!requested)
            return false;
        if (!environment_present("DISPLAY") && !environment_present("WAYLAND_DISPLAY"))
            reason_ = "NO_DISPLAY";
        else
            reason_ = probe_display(probe_path, timeout);
        if (!reason_.empty())
            return false;
        try
        {
            cv::namedWindow("marker debug", cv::WINDOW_AUTOSIZE);
            active_ = true;
        }
        catch (const cv::Exception &)
        {
            reason_ = "WINDOW_OPEN_FAILED";
        }
        return active_;
    }

    // 原 imshow/waitKey 混在 runner；只有 active 才调用，并分别记录 Visualize 与 Wait 成本。
    bool DisplaySession::show_and_poll(const cv::Mat &overlay)
    {
        visualize_elapsed_ = wait_elapsed_ = std::chrono::nanoseconds{};
        if (!active_)
            return false;
        try
        {
            auto start = Clock::now();
            cv::imshow("marker debug", overlay);
            visualize_elapsed_ = Clock::now() - start;
            start = Clock::now();
            const int key = cv::waitKey(1);
            wait_elapsed_ = Clock::now() - start;
            return key == 27 || key == 'q';
        }
        catch (const cv::Exception &)
        {
            try
            {
                cv::destroyWindow("marker debug");
            }
            catch (const cv::Exception &)
            {
            }
            active_ = false;
            reason_ = "DISPLAY_RUNTIME_EXCEPTION";
            return false;
        }
    }

    // 原退出没有独立窗口资源所有者；只关闭本会话创建的窗口，异常不逃出析构。
    DisplaySession::~DisplaySession()
    {
        if (active_)
            try
            {
                cv::destroyWindow("marker debug");
            }
            catch (const cv::Exception &)
            {
            }
    }
} // namespace mark
