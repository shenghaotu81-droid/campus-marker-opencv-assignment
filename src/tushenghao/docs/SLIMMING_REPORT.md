# vision 初步瘦身（第一批）修改报告

> 后续状态：G2预算／消歧和视频容器已在a582c48之后更新，见[G-01/G-02报告](G01_G02_VALIDATION_REPORT.md)。本文的历史复跑数字和旧输出名保留原时点含义；现行执行顺序见[flow-guide](flow-guide.md)。

> 历史文档：记录第一批（基线 b600635）的完成状态／建议。后续 match-only 实施已完成，当前结论和验证以 [MATCH_ONLY_REFACTOR_REPORT.md](MATCH_ONLY_REFACTOR_REPORT.md) 为准；本文旧的“尚未实施／保留D05–D09”等描述属于当时状态。

本批代码和验证已完成，工作分支为 `refactor/vision-lean`，起点 `e09970c`。交付代码、本文、原位置的 `src/tushenghao/docs/flow-guide.md`，另交付根目录 `CORNER_FIT_REFACTOR_PROPOSAL.md`。

## 来源与本次确定的范围

唯一任务文件：`/mnt/d/Personal/Study/26-27Fall/PNX/校内赛/视觉算法开发/开发计划路线板块/全板块链路/搬入新仓库前重构/codex-task.md`；同目录旧 prompt 和图片未作为任务要求。

用户后续明确的调整已执行：

1. **本批保留 D05–D09。**原先的依赖阻塞已解除；删除对应与仿射，等替代检测规则确定后另做。本批没有把它们改名或藏入拟合。
2. **原定角点瘦身照常实施**：误差／extension 门改标签、自检迁入测试、按新命中分布合并路线。
3. **更大的角点重构只写建议**：建议书最终以四边形直接给边、固定映射给身份、完整外框存在性为前提，不传仿射和 hypothesis。已补充不同拟合／路线方案的比较，推荐先验证“普通 L2 主路 + 单个周期后验回退”，未实施此架构。
4. flow-guide 保留在原位置；仅报告和额外建议书放根目录。

没有切换或改动 `refactor/lean`、`main`。根 `docs/` 和 `src/tushenghao/docs/` 除 flow-guide 外均无差异；`tools/calibration/` 整树原样保留。明确删除的工具目录连同其自有夹具、说明和证据材料一并移除。

## 逐项修改

以下路径除根目录交付文件外，相对 `src/tushenghao/`。验证编号见下节。

| 清单项 | 实际修改与位置 | 验证 |
|---|---|---|
| provenance 构建链 | 删除 `cmake/`、MARK_COMMIT/MARK_CODE_HASH 等宏、源码闭包登记与调用；不再生成或读取运行身份指纹 | A、最终构建 |
| audit / validation / fixture / synth_ref | 删除 `tools/audit`、`tools/validation`、`tools/block3_fixture`、`tools/synth_ref`、`src/synth_ref`、`include/vision/synth_ref`、`tests/synth_ref` 及 dispatcher 引用；删除 VISION_BUILD_FIXTURE_TOOLS | A |
| common 及测试依赖 | 保留 `tools/common` 目录；旧取证 reader/digest 随装置删，无空编译／转发目标。拟合单元测试实际需要的像素计数小函数移到 `tests/support`；独立拟合枚举参考也只链接测试，不保留 benchmark 入口 | A、D |
| 面积预算 | 删除 `min_quad_area_px2_`、QUAD_BELOW_APPROVED_AREA 分支与配置／测试引用；budgets_ready 只读两个重投影预算。非退化四边形检查仍保留 | B、D |
| 角点误差、线拟合残差门 | `corner_edge_fit.cpp` 不再因相应误差超限而拒绝；数值留在 CornerEvidence，`detection_publication.cpp` 发布聚合质量旗 | D、录像对比 |
| extension 门 | fitter 和 validator 不再据 extension 超限丢角；保留 `support_extension_px_`，发布质量旗。阈值继续用于标签，未改数值 | D、录像对比 |
| 自检迁入测试 | 生产 `corner_evidence_validation.cpp` 只保留必要来源、完整性和数值检查；原完整像素重拟合、参数／残差／延伸 claimed 值核对迁到 `tests/support/detection/corners/corner_evidence_check.cpp`，包含篡改负例 | D |
| 质量标签传递 | `Detection.quality_flags`：1=拟合残差高、2=角误差高、4=延伸高；`PoseObservation.corner_quality_flags_` 和位姿 IO 保留这些标签。G2 不据此重设质量门，仍用自身 screening；G4 仍用自身准入与 NIS | B、D |
| 13 路合并 | 先松质量门、完整测命中，再删与 2/4 路完全重复的原 8/9 路；生产改为 11 个不同尝试，共用原 stage，删除策略选择、重复结果缓存与复制 trace 的调度。零命中退路用紧凑循环保留，未凭单段录像删除不同估计器／支持域 | D、1676 帧双跑 |
| 双别名与内部头转发层 | 只保留一套实际 vision_* 模块目标；删除 vision::x / mark_x 双别名和各模块 *_internal_headers；消费方直接声明私有头路径，OpenCV 只找实际需要的组件，移除窗口组件依赖 | C、D、构建组合 |
| CLI | 删除 --check-config、--run-purpose、--expected-frames、--mode、--run-dir 及贯穿分支；窗口与 export-video 旗也随路径删。最终无无值开关，使用同一严格 parser，不留原三个分散分支；仅 --video / --camera / --config | C、10 个 CLI 负例 |
| observability 装置 | 删除 diagnostics_context、DetectorDiagnosticsAccess、recorder、frame_record、record_codec、逐阶段取证计时、manifest、environment、fingerprint、bookkeeping、level／采样配置；保留紧凑 run_report 摘要、错误和可选整段耗时 | C、D |
| G2 时间与控制可用性 | 位姿保留 CaptureStamp 中的曝光／时间不确定度；不再在 G2 用时间预算卡 control_usable，Unique 即可用。时间资格留给下游，未知不填 0 | B、D |
| G4 单时间上限 | 删除 config 的 max_time_uncertainty_s_，只保留 profile 的 validated_time_bound_s_；删除 config/profile 的 time_policy_ 及 bounded_only 缺界构造拒绝、测量清空 | B、D |
| G4 时间分支连带调整 | interval_resolved 的关联阻断改为重叠区间时间标签为 false；数值继续名义 dt。predict/bootstrap/reseed 仅在明确有界且 dt 可分辨时传播区间界，否则用名义时间；缺界 optional 不再被盲解引用。恢复时间质量不再强制重建轨迹 | B、D |
| 时间噪声 | 按给定时间不确定度增广 R；缺该字段时用现有 profile 界，缺界／零代用尺度时用既有 max_dt 尺度作条件数值增广。这个代用尺度不被发布为物理时间界；明确 0 的数值不改写。标签及 qualified 仍按真实时间信息判断 | B、D 的未知／重叠时间回归 |
| G4 冻结输出 | 保留滤波 3D 位置、表观速度、协方差、生命周期和合格标签；删除 model_hardware_verified_。公式仅为 measured + 有速度 + position_std 达标 + all_time_bounded_；NIS 门和当前 Unique 测量准入不变 | B、D |
| 手动 verified 旗 | 删除 pose 相机／图纸／预算／selection／Tag 旗；删除 tracking profile、PositionNoise、CameraTransform、measurement 及累计 verified 链；清掉配置、读写与相应旧测试。无消费者的 G2 evidence 配置键也删 | B、D |
| STABLE / display_hold | 删除 temporal_geometry、temporal_stabilizer、stabilize_stage、display_history 四个实现及头、temporal_types、TrackResult/DisplayState、时序配置和相关测试。G2 直接吃 RAW，缺检测显示由 G4 coast 提供 | C、D |
| ExecutionScope / subset | 枚举、参数、scope 分支、scope 审计测试与 subset 完整性机制全删；runOffline 最终只有 app、video、config_path 三参数 | C、D |
| 全链离线入口 | `marker_app` 一进程调用独立 Detector → PoseEstimator → TargetTracker；Tag 白名单非空时走现有 Tag 分支。删 pose_app / tracking_app。完整构建逐帧输出 observations.yaml、tracks.yaml、summary.yaml | C、D、实拍／透视夹具 |
| 可视化与视频 | 仅 `src/app` 读取主线值：克隆图像，叠加检测角、位姿投影、轨迹位置／表观速度和 coast；无条件写视频，不回馈算法。窗口、display_probe、playback_fps、display 段及输出死键全删，fourcc/filename 保留 | C、D、视频读回 |
| 保留项 | D05–D09、IPPE、G2 screening、NIS、VISION_BUILD_TOOLS / MARK_BUILD_G2 / MARK_BUILD_G4、src/app 原名、calibration_tool 原文件均保留。refine_lm 分支、字段及 refine case 已删，未换 IPPE_SQUARE | B、D、差异检查 |
| 等硬件事项 | K 仍直接从 calibration.yaml 读取；内参必须由用户标定。G2 后续在板子录像上验证按 fx 比例缩放的预算分布；G-02/G-03 实物量测后置，不新增手动认证旗或自动阈值填充 | 本批未冒称实物验收 |

没有新增抽象层或算法调度框架。代码／配置／测试／工具／CMake 文本范围净减少约 1.88 万行；新增加的主要是质量标签贯穿、全链连接和实际回归用例。

## 分块验证与最终检查

| 编号 | 时点 | 结果 |
|---|---|---|
| 基线 | e09970c，G2/G4 均开启 | 构建成功，27/27 测试通过 |
| A | 删除工具与 provenance 后 | 全量构建成功，27/27 通过 |
| B | G2/G4 预算、时间和验证旗精简后 | 全量构建成功，pose / Tag / tracking 相关 4/4 通过 |
| C | STABLE／取证／窗口删除，全链连接后 | 全量构建成功，22/22 通过 |
| D | 质量门改标签、自检迁移、路线合并与全链回归后 | 全量构建成功，23/23 通过 |
| 最终完整构建 | build，G2=ON、G4=ON | 23/23，含透视真值全链、coast、时间质量、IO、图像只读和证据自检 |
| 检测构建 | /tmp/vision-lean-detection-only，G2=OFF、G4=OFF | 构建成功，19/19 |
| G2 构建 | /tmp/vision-lean-g2-only，G2=ON、G4=OFF | 构建成功，21/21；位姿输出使用明确的 G2 v1 协议，不伪称完整 G4 replay |
| 纯库构建 | /tmp/vision-lean-libraries，tools=OFF、testing=OFF、G2/G4=ON | 构建成功，不依赖离线工具、渲染或测试 |
| CLI | 重复、未知、缺值和 7 个已删除开关 | 10 个负例均非零退出并报出相应错误 |
| 范围与空白 | git diff --check；保留分支／文档／标定树对比 | 通过；无未解释的保护范围修改 |

构建与测试：

```bash
cmake -S src/tushenghao -B build -DMARK_BUILD_G2=ON -DMARK_BUILD_G4=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

测试包含连续像素、圆角、碎边、近平行、缺边／出框、物理角映射、多解、原图／float 边界、拟合参数已知真值、claimed 值篡改、NIS、协方差、事务回滚、未知及重叠时间区间等。删除功能的测试随功能删除；保留数学 oracle 不属于已删除审计入口。

## 1676 帧：路线合并前后精度对比

输入为 `data/raw/marker_video.avi`，1440×1080，共 1676 帧。SHA256：

`aa1219a7a7b702ea1265be8752853a267c982a651afa0f7846f516f9517c9ac7`

**对比基线是质量门已松开、保留 13 个路由项的版本**，不是原 e09970c 的硬门版本。先统计，再仅合并重复路由；相机、图纸、检测阈值和两项生产重投影预算保持相同。

路线统计由测试侧 `corner_route_test` 使用当前视频观察、原图轮廓和模型邻边执行；没有在生产增加全局计数。以下是候选角调用数，不是检出帧数：

| 指标 | 合并前 | 合并后 |
|---|---:|---:|
| 视频帧 | 1676 | 1676 |
| 成功候选角拟合 | 4360 | 4360 |
| 路线 1（普通 L2） | 4359 | 4359 |
| 路线 4（PhaseMedian 后验位置） | 1 | 1 |
| 其他路线命中 | 0 | 0 |
| 比较样本中的拟合失败 | 0 | 0 |
| 最大对应角点坐标位移 | — | **0px** |
| 逐角残差、corner_error、extension 差异 | — | **逐项相同** |

唯一周期回退样本是 frame_id=784、hypothesis=0、P1。原 8/9 路分别是 2/4 的重复；合并后剩下原 1–7、10–13 对应的 11 个不同尝试。不同模型和支持域的零命中退路没有被单段录像的零统计直接删除。

PnP 比较口径：每个检出帧中，`metrics_valid` 且正深度的候选的最小重投影 RMSE；**仅用于分析，不据此选择发布位姿**。

| 指标 | 合并前 | 合并后 |
|---|---:|---:|
| 检出／有候选帧 | 1088 | 1088 |
| 未检出帧 | 588 | 588 |
| 分析用较小候选 RMSE 中位 | 0.3915354321px | 0.3915354321px |
| P95（nearest rank） | 0.7973333225px | 0.7973333225px |
| 最大值 | 1.1974446734px | 1.1974446734px |
| 实际 Unique / Ambiguous | 0 / 1088 | 0 / 1088 |
| 可视化视频读回 | 1676 帧，1440×1080 | 1676 帧，1440×1080 |

不只是中位数相同：逐角 TSV、完整位姿 YAML、跟踪 YAML 在两次运行中各自逐字节相同。

- 两份 TSV SHA256：`65770697773f354f36d3f7ece26fd0677fd33bac69c3ff2c3d6b27aacdc7841d`
- 两份 observations.yaml SHA256：`c3145aa07424d8937ffd6c32c01d6d224cac2f750d2a3ad393d40c2bfa5b84e2`
- 两份 tracks.yaml SHA256：`b96910c402f8f6fffa6fde2b6eaff2da47230d5a0c9310052bbf930ccc644817`

结论：**这次 13→11 合并没有改变现有录像上的角点结果或 PnP RMSE。**0px 是两版交点的差，不是相对于真实角的绝对误差；录像没有独立角点真值。用户给出的历史好解中位约 0.43px 与这里的松门后样本／筛选口径未完全对齐，不能将 0.391535px 解释成精度提高。

复跑材料在本机临时目录：

- `/tmp/lean-routes-13.tsv`、`/tmp/lean-routes-11.tsv` 和对应 `.log`；
- `/tmp/vision-lean-before-merge/`、`/tmp/vision-lean-after-merge/`：位姿、跟踪、摘要、可视化视频；
- `/tmp/vision-lean-replay-comparison.json`：上述数值与摘要哈希；
- `/tmp/vision-lean-replay/`：复跑用配置副本；跟踪数值 profile 取自已有归档的相机相对合成 profile，仅删已取消的协议字段，数值未改；未写回生产配置。

最终版路线复跑方式：

```bash
./build/corner_route_test data/raw/marker_video.avi /tmp/corner-routes.tsv
./build/marker_app --video data/raw/marker_video.avi --config /tmp/vision-lean-replay/detector.yaml
```

第二条需先在配置副本中指定一个尚不存在的 output.run_directory，避免覆盖已保存的结果。计时两次受并行构建影响，不作为速度提升证据。

## 全链补验与边界

实拍视频在保留的生产两项重投影预算下，1088 个检出帧均有两个候选通过，正确保留 Ambiguous；因此 G4 本次没有收到实拍 Unique 测量，也没有产生实拍三维轨迹。没有改预算、开启新约束或强选候选来制造跟踪。G-01 在真实 Unique 上验 G4 仍是后续任务，不能用本次结果宣布闭环。

另从图纸和已知透视位姿生成 24 帧测试录像（相同 K/dist，两个全黑缺测帧），用明确的理想测试预算和 profile 跑完整 marker_app：**22 帧 Unique → Measured，2 帧未检测 → Coasting**，产生位置与表观速度、逐帧位姿／跟踪记录和视频。录像时间质量保持未知，time_bounded 和 qualified_for_g5 均为 false。该场景也已加入 `offline_chain_test`，不只依靠人工运行；测试专用预算/profile 不进入生产配置。

本仓本批没有实现 G3 决策层；已验证的是 G4 时间标签与 qualified 的输出，后续 G3 必须明确消费这些标签和其他质量信号，不能称为已完成 G3 实物验收。

窗口和 STABLE 已删；coast 轨迹可直接显示。相机入口已实现，当前环境没有实际相机，未冒称在线采集、真实曝光时间界或板子录像验收。相机 K/dist 必须另行正确标定，G-02/G-03 实物量测后置。

## 提交与待办

代码按逻辑提交在目标分支：

- `18b3941` `[tools] 删除作业审计夹具与来源证据构建链`
- `34199a5` `[pose] 精简位姿预算与跟踪时间质量及验证旗链`
- `a0db6f4` `[pipeline] 删除二维稳定与取证路径并接通全链离线视频输出`
- `afb54e2` `[vision] 发布角点质量标签并合并拟合路线及补齐全链回归`
- `eb8166e` `[build] 清除已无共享实现的工具转发目标`

本文、更新的 flow-guide 和建议书随后单独提交，随代码一起推送到已有 upstream。

当前没有需要停下等待用户决定的实现阻塞。保留的后续事项是已明确后置的 D05–D09 替代检测、建议书中更大的四边形／拟合重构、真实 Unique 的 G-01，以及硬件相关 G-02/G-03 和实际相机标定。建议书推荐下一轮先量测两条互补路线；本批生产仍为已验证的 11 个不同尝试，没有擅自实施建议。
