# 112帧恢复：main分支干净实现（Codex执行提示词）

## 目标

在 `fix/112-recovery` 分支上，根据沙盒验证的方案，干净实现112帧漏检修复 + 性能优化。选择**快速路线**（REPORT 5.3 第8条的优先级顺序）。

验收目标：
- 完整视频1676帧：1088检测，112/112恢复
- 原976帧 raw 四角逐值不变（与 main 基线对比）
- 集合外新增检测为 0
- 均值 ≤25ms，最大 ≤33ms，超33ms帧数为 0（Release，单线程，顺序运行）

## 必读输入（按顺序读）

1. `src/tushenghao/docs/evidence/112-sandbox/EXECUTION_PLAN.md`
   - 第1节：推荐组合（原成功路径 + A/B补查 + C1周期模型 + 性能优化）
   - 第2节：三类根因（A/B/C），不是阈值问题
   - **第3节：逐文件实施方案（6步），这是实现的主干**
   - 第6节：0.5阈值的量纲依据（整数栅格量化误差）

2. `src/tushenghao/docs/evidence/112-worstframe/REPORT.md`
   - **第5.3节：main上必须追加的具体实施（8条），这是性能部分的主干**
   - 第5.4节：正确性变化清单（21帧角点变化已列明）
   - 第5.5节：结论边界（只保证固定1676帧基准，不保证任意输入）

沙盒分支 `debug/112-forensics` 上的代码**仅供参考思路，绝不复制**。所有实现必须在新分支上干净重写。

## 实施步骤（按顺序，每步独立验收后再下一步）

### 第一步：帧内原图观测索引（EXECUTION_PLAN 3.1）

- 修改 `lib/corners/corner_observation.hpp/.cpp`：新增内部 `OriginalContourIndex`，持有当前帧原图分割轮廓、component映射缓存
- 在 `lib/pipeline/decode_stage.cpp::decodeStage` 内创建，由 `corner_resolver` 内部入口显式传递
- 灰度、阈值、完整原图 findContours 每帧最多一次；惰性准备，无待解析假设时不分割
- 缓存生命周期限定一次 decode；key用component ID，不跨帧复用
- **验收**：关闭A/B/C、仅保留索引，完整运行1676帧，与原库 status/raw四角差异为 0

### 第二步：弧对搜索的精确等价优化（EXECUTION_PLAN 3.2）

修改 `corner_edge_fit.cpp::fitObservedEdgePair` 内部：
1. 预计算每个像素的两个有限边位置mask，构造前置位置模式连续弧时做AND
2. 缓存像素坐标的首次轮廓索引（保留原find的"首次"语义）
3. 循环弧长前缀表，超预算先拒绝（保守舍入界 `64*epsilon*N*max(1,totalLength)`）
4. 拟合残差循环顺便记录支持AABB，无交集时跳过共享像素集合构造
5. 全部质量谓词通过后，仍对**所有候选**执行几何歧义检查，不提前跳过

**验收**：976帧 status/raw 全等

### 第三步：A/B失败补查（EXECUTION_PLAN 3.3）

- 旧候选/位置模式成功直接返回
- 失败时依次：旧端点+后验有限段位置 → 全部真实端点+原位置模式
- 后验位置：实测支持投影到拟合线，投影点对指定有限模型边的最大距离仍 ≤9.0
- 全端点阶段按完整有序支持去重，严禁按端点坐标/拟合参数去重
- **验收**：976→996，目标新增20，原成功输出全等，集合外新增0

### 第四步：C1周期模型与估计器（EXECUTION_PLAN 3.4）

- 内部模型：近竖直 `x=a(y-y0)+b+c*h(y)`，近水平对称；h为扫描行奇偶±1
- a/b/c用中心化充分统计量闭式2×2解；固定斜率时用两相位中位数得b/c
- 仅当原拟合+A/B+相位中位仍失败时，对斜率做L1目标精化（40次凸区间搜索+拐点枚举）
- 失败路由：旧路径→B→A→C1原端点原位置→C1原端点后验位置→C1全端点原位置→L1精化
- 路由封装在实例内策略，**不使用任何 `MARK_*` 环境变量开关**
- **验收**：112/112恢复

### 第五步：validator与证据契约（EXECUTION_PLAN 3.5）

- `lib/corners/corner_types.hpp::CornerEvidence` 增加模型类型、轴、c、校正残差等**内部**元数据
- `corner_evidence_validation.cpp::validateCornerEvidence` 从完整原始有序支持重算两类残差、重建参数
- 原 `line_mean_residual_px_` 等字段定义不变，不能把校正误差写入旧字段
- **不改** `include/mark/*`，**不改**公开 `Detection` 结构体

### 第六步：最慢帧优化（REPORT 5.3，8条）

1. fit调用范围scratch，按 `(modelKind, firstRawIndex, lastRawIndex)` 缓存拟合结果
2. 惰性计算端点欧氏剔除邻域（`cv::norm < turn_trim` 原式），保持原枚举顺序
3. 唯一点数检查达下限即停；共享像素集合延迟构建；全端点序列去重保留
4. 按支持legacy首次坐标索引建front桶，候选j排序回原索引顺序
5. 每搜索阶段缓存纯几何配对结果；**每个别名仍无条件执行歧义检查**，再评分/tie-break
6. 固定8槽拒绝计数替代字符串map；整数奇偶用精确转换；AABB极值算轴向extension
7. 零斜率估计建成内部纯函数；`corner_evidence_validation.cpp` 增对应模型类型
8. **优先级顺序**：原失败后，先在原端点依次验证（相位中位+原位置、零斜率+原位置、相位中位+后验位置、零斜率+后验位置），再执行完整B/A/C/L1补查

**验收**：均值≤25ms，最大≤33ms，超33ms帧数为0

## 硬约束（违反即返工）

1. **中文注释**：每个修改处必须用中文注释说明"原来什么问题、为什么这样改"，不能是黑盒。用户在本次修改完成后要自己通读一遍所有AI写的代码，注释是写给他看的，必须让人能看懂因果。
2. 不改 `docs/ref/` 冻结文档、不改公共签名、不改 `Detection`/`FrameResult` 布局
3. 不为过视频放宽阈值：0.5/9.0 保持不变
4. 不复制沙盒代码，不复制任何 `MARK_*` 环境变量开关
5. 公开接口、Detection、YAML 配置全部不变
6. 单线程，不引入多线程；正式helper用显式immutable输入+局部scratch，不用thread_local
7. 不删慢帧，不用超时熔断掩盖算法问题
8. **代码格式**：严格遵守 `src/tushenghao/.clang-format`（LLVM基准、4空格缩进、100列上限、Allman大括号、注释不自动重排）。提交前用 clang-format 检查新增/修改的代码

## 文档与归档（必须做，不是可选）

1. **实现记录**：`src/tushenghao/docs/fix112_recovery_log.md`
   - 参照 `src/tushenghao/docs/fix2_followup_log.md` 的格式
   - 记录每步做了什么、为什么这样改、验收结果
   - 这是用户后续自己读代码时的路线图，要写清楚

2. **证据归档**：`src/tushenghao/docs/evidence/112-recovery/`
   - 每次完整验证的 JSONL、日志、manifest 都归档到这里
   - 不许只放 `build/`（gitignore 临时目录）
   - build 用固定实验目录，跑完归档即清理

3. **证据索引**：完成后更新 `src/tushenghao/docs/evidence/INDEX.md`，加一行 112-recovery 入口

4. **主文档索引** `src/tushenghao/docs/INDEX.md`：在 Final-fixes 条目下方添加112恢复条目，格式参照现有条目：
   - `[112恢复施工记录](fix112_recovery_log.md)`：链接到实现记录
   - `[112恢复验收](fix112_recovery_acceptance.md)`：链接到验收文档

5. **验收文档**：新建 `src/tushenghao/docs/fix112_recovery_acceptance.md`
   - 参照 `src/tushenghao/docs/fix2_followup_acceptance.md` 的格式
   - 记录验收标准、实测数据（均值/P50/P95/P99/最大/超33ms帧数）、通过情况、已知边界（2px压力盲区、固定1676帧基准）

6. **README更新**：
   - 先将当前 `src/tushenghao/README.md` 完整复制到 `src/tushenghao/docs/history/README_before_112_recovery.md`（参照 `README_before_fix2_followup.md` 的先例，原样保留）
   - 再更新 README 的"路径与已知限制"段落：将"本轮小修没有解决闪烁"改为112恢复已落地（1088/1676、112/112、均值7.5ms/最大29.5ms/0超33ms），保留诚实边界（2px压力盲区、结论限于固定1676帧基准）

## 最终验收（全部必须通过）

- [ ] 完整视频1676帧：1088检测，112/112恢复，原976帧raw四角逐值不变
- [ ] 集合外新增检测为 0
- [ ] 均值 ≤25ms，P50/P95/P99/最大如实记录，最大 ≤33ms，超33ms帧数为 0
- [ ] 25/25 回归测试通过（Release + Debug断言启用）
- [ ] 20/20 负样本全部拒绝
- [ ] 独立合成测试：固定样本（见112-sandbox/summary.json），67/68通过、最大真值误差<2px
- [ ] 证据已归档到 `src/tushenghao/docs/evidence/112-recovery/`，含manifest
- [ ] `fix112_recovery_log.md` 已写完，`fix112_recovery_acceptance.md` 已写完，中文注释已覆盖所有改动点
- [ ] `evidence/INDEX.md`、`docs/INDEX.md` 已更新；README已归档旧版并更新"已知限制"段落
