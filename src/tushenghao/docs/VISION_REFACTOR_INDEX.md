# 视觉瘦身与 match-only 文档索引

当前交付：[单候选拟合与156项瘦身报告](SINGLE_CANDIDATE_SLIMMING_REPORT.md)、[逐项落实表](THRESHOLD_GATE_LABEL_IMPLEMENTATION.md)、[flow-guide](flow-guide.md)、[规则v2](MATCH_ONLY_RULES.md)。1676帧：1088检测/4352角、PnP RMSE中位/P95/最大0.357573/0.690671/0.926320px。G2保留两解并给名义测量，G4联合改为接收Ambiguous并消费分歧；不再沿用1.10/0.25/Unique-only资格。生产profile为空，条件回放不等于实测速度标定。

[固定映射报告](FIXED_MAPPING_REPORT.md)、[G01/G02报告](G01_G02_VALIDATION_REPORT.md)、[match-only上一轮报告](MATCH_ONLY_REFACTOR_REPORT.md)、[第一批建议](CORNER_FIT_REFACTOR_PROPOSAL.md)是历史依据；其两路线/质量旗/G2门控描述按当时版本理解。

历史材料（基线b600635，旧“保留D05–D09／建议未实施”为当时状态）：

- [SLIMMING_REPORT.md](SLIMMING_REPORT.md)：第一批瘦身报告。
- [CORNER_FIT_REFACTOR_PROPOSAL.md](CORNER_FIT_REFACTOR_PROPOSAL.md)：第一批建议与本轮评估回填。

这些任务文档由根目录统一移到src/tushenghao/docs整理；其他历史证据快照保留原样，不参与当前构建。
