# 文档索引

当前实现先读[README](../README.md)与[Block3当前状态](block3.md)，再查[预算及审批](block3_budget.md)、[适用边界](evidence/block3/05-L-observation-v5-v7/适用边界与已知局限.md)。Block3/4/5 已合入；算法基准为冻结v7，H已PASS，人工 V/Q 等验收仍待完成；Block4代码及G-B已批准落地，批准范围内Block4验收已完成，公共流程按预算就绪，未标注视频不报正确率。

| 入口 | 内容 |
|---|---|
| [Block4验收](block4_acceptance.md) | 已实现、主动断言/回归证据、决策与审批、最终结果及范围外限制 |
| [Block4预算](block4_budget.md) | C/H定位统计、固定平滑网格对照、生产数值已批准 |
| [Block3当前状态](block3.md) | 当前模块路径、规则、验证、D01–D25及剩余项 |
| [预算表](block3_budget.md) | 审批与统计口径；早期记录是历史，不代表当前待审 |
| [合成器复用](block3_fixture_reuse.md) | 固定C/H来源与工具复用记录 |
| [冻结参考](ref/) | 原始冻结依据，路径/内容未修改 |
| [证据总索引](evidence/INDEX.md) | 分批结果、原始数据与验证日志 |
| [架构索引](architecture/INDEX.md) | include分析、源码迁移、文档/工具整理 |
| [工具索引](../tools/INDEX.md) | 用途、构建入口、命令及退出语义 |
| [Block3历史](history/block3-before-final-organization.md) | 完整过程及已撤回计划，仅作历史对照 |

[block1.md](block1.md)、[block2.md](block2.md)、[corner_semantics.md](corner_semantics.md)保留原记录；其中旧路径/阶段计划按记录时点理解，现行实现以Block3当前状态和README为准。

冻结总汇总有6条原有引用指向未提供文档，ref原文保持；缺失清单见[整理验证](architecture/docs-tools-organization.md)。当前可编辑导航均已校验。

- [Block5验收](block5_acceptance.md)：Route B基线、实际测试/性能/异常与剩余项。
- [Block5诊断schema](diagnostics_schema.md)：时钟、空值、事件、范围、采样、指纹和失败语义。
- [Block5证据](evidence/block5/INDEX.md)：逐帧金样、运行与完整性核查。

- [Final-fixes 验收](final-fixes_acceptance.md)：13 项实施、两种构建、完整回归、显示与视频自动验证及人工关卡。
- [Final-fixes 证据](evidence/final-fixes/INDEX.md)：批次日志、全帧 JSONL、完整 MP4、命令及 SHA256。
- [112恢复施工记录](fix112_recovery_log.md)：干净重写、六步独立验收、中文代码阅读路线。
- [112恢复验收](fix112_recovery_acceptance.md)：1088/1676、112/112、单线程完整性能与独立测试边界。

当前 Final-fixes 自动验证已完成；有屏显示和人工播放器待确认，干净 Linux 按用户决定记未验证。
历史 Block5 失败 fixture 与未实现视频导出不代表当前状态；原记录保持。

Path A 推荐顺序：[专项验收](path_a_acceptance.md) → [施工日志](path_a_fix_log.md) → [永久证据](evidence/final-fixes/path-a/) → [132帧review索引](evidence/final-fixes/path-a/frames/review_index.csv)。两种构建25/25，Release/Debug完整1676与专项机器核查均PASS，逐帧公共结果零差异，用户132/132 review已通过；原受阻记录与批准后的进展均保留。

- [Fix2-sweep 后续小修施工记录](fix2_followup_log.md)：计数、几何残差、README 迁移和独立排版。
- [Fix2-sweep 后续小修验收](fix2_followup_acceptance.md)：A01–A10 实测、双配置测试和同视频回归。
- [README 历史全文](history/README_before_fix2_followup.md)：原入口完整保留，当前操作见项目 README。
