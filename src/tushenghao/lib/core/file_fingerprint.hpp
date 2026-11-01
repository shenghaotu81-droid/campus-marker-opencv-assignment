#pragma once
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace mark {
// A02/V03：流式字节身份，FNV 与文件长度组成旧协议；错误上下文由调用步骤提供。
inline std::string file_fingerprint(const std::filesystem::path &path,
                                    const std::string &open_error,const std::string &read_error) {
    std::ifstream input(path,std::ios::binary);
    if(!input) throw std::runtime_error(open_error);
    uint64_t hash=14695981039346656037ull,size=0;
    char block[65536];
    while(input) {
        input.read(block,sizeof block);const auto count=input.gcount();size+=uint64_t(count);
        for(std::streamsize i=0;i<count;++i) { hash^=static_cast<unsigned char>(block[i]);hash*=1099511628211ull; }
    }
    if(!input.eof()) throw std::runtime_error(read_error);
    std::ostringstream out;
    out<<"FNV1a64:"<<std::hex<<std::setw(16)<<std::setfill('0')<<hash<<":size="<<std::dec<<size;
    return out.str();
}
}
