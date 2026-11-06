#include "detector/config.hpp"
#include "assertions.hpp"
#include <fstream>
#include <chrono>
#include <iostream>
int main()
{
    try{
        using test_support::check;
        const auto source = std::filesystem::path(VISION_SOURCE_ROOT) / "config/detector.yaml";
        std::ifstream in(source);
        std::string original((std::istreambuf_iterator<char>(in)), {});
        auto temp = std::filesystem::temp_directory_path() / ("single-config-" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()) + ".yaml");
        check(mark::loadConfig(source).detector_config.corner_.physical_to_screen_ == std::array<int, 4>{{0, 1, 2, 3}}, "fixed mapping");
        for (auto change:{std::pair{"[0, 1, 2, 3]", "[0, 0, 2, 3]"}, std::pair{"[0, 1, 2, 3]", "[0, 1, 2]"}, std::pair{"[0, 1, 2, 3]", "[0, 1, 2, 4]"}, std::pair{"[0, 1, 2, 3]", "[0, 1, 2, 2.5]"}, std::pair{"work_width: 960", "work_width: 0"}, std::pair{"threshold: 200", "threshold: 300"}, std::pair{"approximation_epsilon: 2.0", "approximation_epsilon: -1."}, std::pair{"schema_version: 2", "schema_version: 1"}})
        {
            auto text = original;
            auto p = text.find(change.first);
            check(p != std::string::npos, "fixture field missing");
            text.replace(p, std::string(change.first).size(), change.second);
            {
                std::ofstream out(temp);
                out << text;
            }
            bool failed = false;
            try{
                mark::loadConfig(temp);
            }
            catch (const std::exception &){
                failed = true;
            }
            check(failed, "bad config accepted");
        }
        auto nondefault = original;
        nondefault.replace(nondefault.find("[0, 1, 2, 3]"), 12, "[1, 2, 3, 0]");
        {
            std::ofstream out(temp);
            out << nondefault;
        }
        check(mark::loadConfig(temp).detector_config.corner_.physical_to_screen_ == std::array<int, 4>{{1, 2, 3, 0}}, "configured mapping lost");
        std::filesystem::remove(temp);
        return 0;
    }
    catch (const std::exception &e){
        std::cerr << e.what() << '\n';
        return 1;
    }
}
