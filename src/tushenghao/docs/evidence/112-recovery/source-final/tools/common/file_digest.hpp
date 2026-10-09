// 仅离线工具使用OpenSSL计算输入/配置摘要；Detector库不依赖OpenSSL/JSON。
#pragma once
#include <openssl/evp.h>
#include <fstream>
#include <memory>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace audit
{
    // 流式SHA-256，不把整段视频读入内存，也不通过shell拼接用户路径。
    inline std::string sha256(const std::string &path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            throw std::runtime_error("cannot hash " + path);
        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(),
                                                                    EVP_MD_CTX_free);
        if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1)
            throw std::runtime_error("SHA256 init failed");
        char block[65536];
        while (file.read(block, sizeof block) || file.gcount())
            if (EVP_DigestUpdate(ctx.get(), block, static_cast<size_t>(file.gcount())) != 1)
                throw std::runtime_error("SHA256 update failed");
        if (!file.eof())
            throw std::runtime_error("hash read failed " + path);
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int length = 0;
        if (EVP_DigestFinal_ex(ctx.get(), digest, &length) != 1)
            throw std::runtime_error("SHA256 final failed");
        std::ostringstream out;
        for (unsigned int i = 0; i < length; ++i)
            out << std::hex << std::setw(2) << std::setfill('0') << int(digest[i]);
        return out.str();
    }
}
