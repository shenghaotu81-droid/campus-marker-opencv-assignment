#pragma once
#include <stdexcept>
#include <string>
namespace test_support {
// Release 同样执行，失败说明由用例提供；不合并任何数学 oracle。
inline void check(bool condition,const std::string &message) {
    if(!condition) throw std::runtime_error(message);
}
}
