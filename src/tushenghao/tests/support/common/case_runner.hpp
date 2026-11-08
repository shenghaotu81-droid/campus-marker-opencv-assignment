#pragma once
#include <iostream>
#include <string>
#include <utility>
#include <vector>
namespace test_support {
// 逐例执行并保留PASS/FAIL 格式，异常不跳过后续用例。
inline int run(const std::vector<std::pair<std::string,void(*)()>> &cases) {
    int failures=0;
    for(const auto &item:cases) {
        try { item.second();std::cout<<"PASS "<<item.first<<'\n'; }
        catch(const std::exception &error) { ++failures;std::cerr<<"FAIL "<<item.first<<": "<<error.what()<<'\n'; }
    }
    return failures?1:0;
}
}
