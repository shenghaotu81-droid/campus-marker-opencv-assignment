# 视觉瘦身与 match-only 文档索引

固定安装分支最新交付：[固定映射报告与赛前核验](FIXED_MAPPING_REPORT.md)。匹配只识别完整目标，坐标排序／安装映射为独立步骤，G2/G4与拟合保持10eae9b行为。

当前实现与复核入口：

- [G01_G02_VALIDATION_REPORT.md](G01_G02_VALIDATION_REPORT.md)：a582c48后G2消歧、真实Unique到G4条件复验、NIS与MJPG AVI完整读回。

- [MATCH_ONLY_RULES.md](MATCH_ONLY_RULES.md)：比赛尺寸／规则依据、match-only契约、两路线与质量原则。
- [MATCH_ONLY_REFACTOR_REPORT.md](MATCH_ONLY_REFACTOR_REPORT.md)：a582c48的match-only代码改动、构建、1676帧与真值对照、MaixCam待验项。
- [flow-guide.md](flow-guide.md)：原位维护的执行顺序、输入输出和替换点。
- [validation/match-only/metrics.json](validation/match-only/metrics.json)：统计及可复算数据。

历史材料（基线b600635，旧“保留D05–D09／建议未实施”为当时状态）：

- [SLIMMING_REPORT.md](SLIMMING_REPORT.md)：第一批瘦身报告。
- [CORNER_FIT_REFACTOR_PROPOSAL.md](CORNER_FIT_REFACTOR_PROPOSAL.md)：第一批建议与本轮评估回填。

这些任务文档由根目录统一移到src/tushenghao/docs整理；其他历史证据快照保留原样，不参与当前构建。
