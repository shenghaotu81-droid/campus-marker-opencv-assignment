#pragma once
#include "config/app_config.hpp"
#include "config/config.hpp"
#include "pipeline/diagnostics_recorder.hpp"
#include <set>
namespace mark {
// App与audit共用离线消费/记录生命周期；执行scope决定真实算法边界，不冒称publicprocess。
int runOffline(AppConfig,const std::string& video,const std::string& config_path,ExecutionScope scope,
 const std::set<uint64_t>& subset={},const std::string& purpose="production");
}
