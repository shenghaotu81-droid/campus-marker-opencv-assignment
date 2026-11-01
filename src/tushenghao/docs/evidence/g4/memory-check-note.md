# 内存检查执行记录

首轮严格按第7.2节参数在执行沙箱内运行，CTest退出8（0/2）；两项均在LeakSanitizer退出扫描阶段报fatal error，并明确提示ptrace环境不受支持。完整原日志保留在ctest-asan.log。

按执行环境要求申请并取得沙箱外执行授权后，使用相同构建、ASAN_OPTIONS=detect_leaks=1、UBSAN_OPTIONS=halt_on_error=1和相同CTest选择器重跑，未更改代码、编译参数或关闭泄漏检测；CTest退出0（2/2），无AddressSanitizer/UBSan/泄漏报告。证据为ctest-asan-unsandboxed.log及last-test-asan-unsandboxed.log。构建的第三方库未声称全部插桩。
