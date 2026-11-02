#pragma once
#include <filesystem>
#include <string>
namespace mark {
std::string file_fingerprint(const std::filesystem::path &,const std::string &,const std::string &);
}
