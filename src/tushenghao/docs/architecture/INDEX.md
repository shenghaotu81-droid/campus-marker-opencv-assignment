# 架构与整理记录

下表保留原迁移／整理记录；现行流程边界见 [流程导读](../flow-guide.md)，瘦身裁决与未做项见 [修改报告](../lean-changes.md)。本次没有移动文件或拆分模块 CMake。

| 记录 | 用途 |
|---|---|
| [include依赖分析](include-dependency-analysis.md) | 源码迁移前的审批快照，含93文件/227边 |
| [源码目录迁移](layout-reorganization.md) | 6个lib模块、公开include、app与test_support |
| [迁移manifest](layout-reorganization-manifest.json) | 新旧源路径与hash |
| [迁移回归](layout-reorganization-verification.json) | C/H/视频与v7逐条等价 |
| [文档与工具整理](docs-tools-organization.md) | 本轮目录、索引、完整性及构建验证 |
| [整理manifest](docs-tools-organization-manifest.json) | 本轮新旧路径和保留文件hash |

源码迁移回归原始结果/日志在[evidence/layout-reorganization](../evidence/layout-reorganization/)，本轮整理验证在[evidence/docs-tools-organization](../evidence/docs-tools-organization/)。历史分析JSON中的源码路径是当时布局，不改写历史摘要。
