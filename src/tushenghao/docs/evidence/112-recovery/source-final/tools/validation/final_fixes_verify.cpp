#include "common/frame_record_reader.hpp"
#include "pipeline/diagnostics_recorder.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <opencv2/videoio.hpp>
#include <regex>
#include <set>
#include <sstream>

namespace
{
    namespace fs = std::filesystem;
    using mark::quoteJson;
    using mark::validation::readJson;

    // 原验收只看文件存在；完整读取文件且失败非零，输出始终是新报告。
    std::string read_file(const fs::path &path)
    {
        std::ifstream in(path);
        if (!in)
            throw std::runtime_error("cannot read: " + path.string());
        return {std::istreambuf_iterator<char>(in), {}};
    }

    // 原工具没有固定参数契约；重复/未知/缺值立即拒绝，不静默覆盖。
    std::map<std::string, std::string> parse_options(int argc, char **argv)
    {
        if (argc < 2)
            throw std::runtime_error("links|video|console operation required");
        const std::string operation = argv[1];
        std::set<std::string> allowed{"--report"};
        if (operation == "links")
            allowed.insert("--doc");
        else if (operation == "console")
            allowed.insert("--input");
        else if (operation == "video")
            for (auto key : {"--input", "--expected-frames", "--width", "--height"})
                allowed.insert(key);
        else
            throw std::runtime_error("unknown operation");
        std::map<std::string, std::string> args;
        for (int i = 2; i < argc; ++i)
        {
            std::string key = argv[i];
            if (!allowed.count(key) || i + 1 == argc ||
                std::string(argv[i + 1]).rfind("--", 0) == 0 ||
                !args.emplace(key, argv[++i]).second)
                throw std::runtime_error("unknown/duplicate/missing option");
        }
        if (args.size() != allowed.size())
            throw std::runtime_error("missing required option");
        return args;
    }

    // 原本地链接没有统一路径解码；处理 URL 编码与尖括号，保留空格文件名。
    std::string decode_path(std::string value)
    {
        if (!value.empty() && value.front() == '<')
            value = value.substr(1, value.size() - 2);
        auto anchor = value.find('#');
        if (anchor != std::string::npos)
            value.resize(anchor);
        std::string decoded;
        for (size_t i = 0; i < value.size(); ++i)
        {
            if (value[i] == '%')
            {
                if (i + 2 >= value.size())
                    throw std::runtime_error("invalid percent encoding");
                const auto hex = value.substr(i + 1, 2);
                if (hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
                    throw std::runtime_error("invalid percent encoding");
                decoded += char(std::stoi(hex, nullptr, 16));
                i += 2;
            }
            else
                decoded += value[i];
        }
        return decoded;
    }

    // 原 Block4 引用被清理的 build；逐个核本地归档目标，报告每条引用而不是只报总数。
    std::string check_links(const fs::path &doc)
    {
        const auto source = read_file(doc);
        const std::regex link(R"(\]\((<[^>]*>|[^)]*)\))");
        std::ostringstream out;
        out << "{\"result\":\"PASS\",\"links\":[";
        size_t count = 0, block4 = 0;
        for (std::sregex_iterator it(source.begin(), source.end(), link), end; it != end; ++it)
        {
            auto target = (*it)[1].str();
            if (target.empty() || target.front() == '#' ||
                target.find("://") != std::string::npos || target.rfind("mailto:", 0) == 0)
                continue;
            const auto decoded = decode_path(target);
            if (decoded.empty())
                continue;
            auto path = doc.parent_path() / decoded;
            if (!fs::exists(path))
                throw std::runtime_error("missing local link: " + path.string());
            if (count++)
                out << ',';
            if (decoded.rfind("evidence/block4/", 0) == 0)
                ++block4;
            out << "{\"reference\":" << quoteJson(target)
                << ",\"target\":" << quoteJson(path.lexically_normal().string())
                << ",\"exists\":true}";
        }
        out << "],\"checked\":" << count << ",\"block4_archive_links\":" << block4 << '}';
        return out.str();
    }

    // 原视频非空不能证明完整；逐帧读回全部图像，尺寸和期待帧数必须匹配。
    std::string check_video(const std::map<std::string, std::string> &args)
    {
        auto positive = [&](const char *key)
        {
            const auto &token = args.at(key);
            if (token.empty() || token.find_first_not_of("0123456789") != std::string::npos)
                throw std::runtime_error("positive integer required");
            auto n = readJson(quoteJson(token)).u64();
            if (!n)
                throw std::runtime_error("positive integer required");
            return n;
        };
        const auto expected = positive("--expected-frames"), width = positive("--width"),
                   height = positive("--height");
        cv::VideoCapture capture(args.at("--input"));
        if (!capture.isOpened())
            throw std::runtime_error("video open failed");
        uint64_t count = 0;
        cv::Mat frame;
        while (capture.read(frame))
        {
            if (frame.empty() || uint64_t(frame.cols) != width || uint64_t(frame.rows) != height)
                throw std::runtime_error("video size mismatch");
            ++count;
        }
        if (count != expected)
            throw std::runtime_error("video frame count mismatch");
        std::ostringstream out;
        out << "{\"result\":\"PASS\",\"frames\":" << count << ",\"width\":" << width
            << ",\"height\":" << height << ",\"fps\":" << capture.get(cv::CAP_PROP_FPS) << '}';
        return out.str();
    }

    // 原 console 验收只检查非空；要求技术行以及中文实际帧数、耗时/关闭计时和瓶颈说明。
    std::string check_console(const fs::path &input)
    {
        const auto value = read_file(input);
        const std::regex english(
            R"(run=\S+ frames=([0-9]+) fingerprint=[0-9a-f]{16} incomplete=[01])");
        const std::regex chinese(
            R"(帧数：读取 ([0-9]+)，处理 ([0-9]+)；检测 ([0-9]+)，未检测 ([0-9]+))");
        std::smatch line, counts;
        if (!std::regex_search(value, line, english) ||
            !std::regex_search(value, counts, chinese) || line[1] != counts[2])
            throw std::runtime_error(
                "console technical/Chinese frame counts missing or inconsistent");
        if (value.find("运行结果：") == std::string::npos ||
            value.find("完整性：") == std::string::npos ||
            value.find("瓶颈阶段") == std::string::npos ||
            (value.find("耗时：") == std::string::npos &&
             value.find("阶段计时未启用") == std::string::npos))
            throw std::runtime_error("console measured/disabled timing summary missing");
        return "{\"result\":\"PASS\",\"frames\":" + counts[2].str() + "}";
    }
} // namespace

// 验收工具不参与生产算法；错误报告可核查且已有 report 一律保留。
int main(int argc, char **argv)
{
    fs::path report;
    try
    {
        auto args = parse_options(argc, argv);
        report = args.at("--report");
        if (fs::exists(report))
            throw std::runtime_error("report already exists");
        std::string operation = argv[1];
        const auto result = operation == "links"   ? check_links(args.at("--doc"))
                            : operation == "video" ? check_video(args)
                                                   : check_console(args.at("--input"));
        std::ofstream out(report);
        out << result << '\n';
        out.flush();
        if (!out)
            throw std::runtime_error("report write failed");
        std::cout << "PASS\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        if (!report.empty() && !fs::exists(report))
        {
            std::ofstream out(report);
            out << "{\"result\":\"FAIL\",\"error\":" << quoteJson(error.what()) << "}\n";
        }
        std::cerr << error.what() << '\n';
        return 1;
    }
}
