#pragma once
#include <map>
#include <optional>
#include <set>
#include <string>
#include <cstdint>
namespace mark {
enum class OptionError { Unknown, MissingValue, Duplicate, DuplicateFlag };
// A01/V02：只检查 key/value/flag 形状；入口显式给允许项、值策略和旧错误上下文；Fail 必须抛出异常。
template<class Fail>
std::map<std::string,std::string> parse_options(int argc,char **argv,
    const std::set<std::string> &valued,const std::set<std::string> &flags,
    bool reject_option_value,Fail fail,int first=1)
{
    std::map<std::string,std::string> result;
    for(int i=first;i<argc;++i)
    {
        const std::string key=argv[i];
        if(flags.count(key))
        {
            if(!result.emplace(key,"1").second)fail(OptionError::DuplicateFlag,key);
            continue;
        }
        if(!valued.count(key))fail(OptionError::Unknown,key);
        if(i+1==argc || (reject_option_value && std::string(argv[i+1]).rfind("--",0)==0))
            fail(OptionError::MissingValue,key);
        if(!result.emplace(key,argv[++i]).second)fail(OptionError::Duplicate,key);
    }
    return result;
}
// 十进制 uint64 的完整 token；调用方决定是否允许零，并保留自己的报错文本。
inline std::optional<uint64_t> decimal_uint64(const std::string &token)
{
    if(token.empty() || token.find_first_not_of("0123456789")!=std::string::npos)return std::nullopt;
    return std::stoull(token);
}
}
