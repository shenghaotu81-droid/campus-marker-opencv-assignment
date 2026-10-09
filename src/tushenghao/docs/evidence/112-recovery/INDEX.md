# 112-recovery证据索引

本轮从main基线干净重写；方案与历史沙盒保持只读。实现入口：[施工记录](../../fix112_recovery_log.md)、[验收表](../../fix112_recovery_acceptance.md)。

| 内容 | 文件 |
|---|---|
| 统一逐值/集合/性能/独立测试断言 | [summary.json](summary.json)、[核查日志](final-verification.log) |
| 正式两次全帧结果 | [复测1](final-repeat1/frames.jsonl)、[复测2](final-repeat2/frames.jsonl)，各目录含manifest/log/summary及当时生产源码 |
| 原976基线 | [baseline](baseline/manifest.json) |
| 六步中间验收 | [索引](step1-index/summary.json)、[等价搜索](step2-search/summary.json)、[A/B](step3-ab/summary.json)、[周期旧顺序](step4-periodic/summary.json)、[证据契约](step5-manifest.json) |
| 性能未达标探索与最终优化 | [缓存初版](step6-first/summary.json)、[描述scratch](step6-scratch/summary.json)、[估计器scratch](step6-model-scratch/summary.json)；所有1676帧均保留 |
| 双配置25项回归 | [Release](final-release-ctest.log)、[Debug](final-debug-ctest.log)、[真实flags与CTest原日志](build-metadata/) |
| 独立固定样本 | [25条校准](final-calibration.jsonl)、[88条测试](final-test.jsonl)，同名log含总计 |
| 完整reference与参数篡改主动检查 | [最终输出](final-reference.log)、[第五步首次测试前提失败](step5-ctest-first.log) |
| 格式、输入与原README保护 | [格式](clang-format-check.log)、[环境](environment.json)、[开工保护哈希](protected-before.json) |
| 最终可复核来源 | [源码快照](source-final/)、[总manifest](manifest.json)、[复现](REPRODUCE.md)、[清理记录](cleanup.json) |

每个完整视频目录的frame必须连续0..1675，status与raw四角按JSON数值精确比较。探索计时不隐藏，不替代最终冻结复测。时间单位为ms，process包括Summary；媒体解码与序列化在计时外。
