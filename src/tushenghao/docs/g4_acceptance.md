# G4 实施验收记录

当前入口：match-only规则、角拟合两路线与最新G2筛选见[MATCH_ONLY_RULES](MATCH_ONLY_RULES.md)、[flow-guide](flow-guide.md)和[G-01/G-02报告](G01_G02_VALIDATION_REPORT.md)。新代码真实录像1054 Unique／34 Ambiguous／0 NoSolution，G4已完成原profile数值下的条件实测链验证；生产profile仍空，G-02真实速度协方差校准与硬件时间验证仍OPEN。下文保留旧阶段验收的原时点含义，旧入口／预算／关卡不代表现行代码。


本表记录实际分支施工结果；编写者隔离预验证不能直接复制成用户已验收。四栏分开：工程完成、离线证据、尚未证明、硬件关闭条件。

## 完成内容

| 项目 | 实施状态 | 证据 |
|---|---|---|
| 原Detector/G2源码及公共布局保持 | 完成；5095个初始文件仅指定三处追加，G2零差异；见[evidence/g4/freeze-check.json](evidence/g4/freeze-check.json) | evidence/g4/原文件保护清单 |
| 独立tracking库、实际dt、Joseph与MSE传播 | 完成；20文件落盘、双构建、R2数值断言通过；见[evidence/g4/r2-and-review.md](evidence/g4/r2-and-review.md) | 构建/契约测试 |
| 关联、生命周期、独立重建与只读forecast | M01–M12/T01–T51在Release/Debug/ASan均通过 | M01–M12/T01–T51 |
| profile标定/三倍标准差/独立holdout | 四域独立完成，每域6项总体/12项分层统计、12849帧holdout、128坏量测全部拒绝 | calibration.md/verification.md/CSV |
| 兼容reader/CLI/中文文档与导航 | I01–I09通过；11个CLI负例命名拒绝，显式零界合法；三处导航已追加 | I01–I09/README/INDEX |

## 验证方法

| 必须项 | 通过标准 | 实际结果 |
|---|---|---|
| Release/Debug | 原27项+G4两项，均29/29；不得删测试或改旧阈值换绿 | Release=29/29（9.82s），Debug=29/29（50.62s）；配置/构建/CTest退出0，原测试未修改 |
| 数学/契约 | M01–M12、T01–T51全过，首帧不报零速、七态不偷测量、时间/坐标拒绝正确 | 全通过；R2 Ppp_xx直接断言期望0.0206987654320988m²、容差1e-12；缺界验证/构造命名拒绝、零界合法回归通过 |
| IO | I01–I09全过；大整数/空帧/旧格式/未测状态无伪造 | Release/Debug/ASan均通过；大整数、空帧、旧格式保持；旧1676帧曝光/时间误差全部空 |
| 标定 | QR仅train；门槛仅selection；每项n/均值/样本std/3σ/来源；holdout不调参 | 3 train/2 selection/3 holdout完整会话，四域各自profile；每项n/μ/std/3σ/来源及12项分层均已核对，holdout不调参 |
| 对照 | raw/一阶/KF共同样本与全部覆盖、反转、误接受、包含率、失败均保留 | raw/一阶/KF全部及共同样本、覆盖、反转、经验包含率、全量CSV和tracks均保留；速度包含率不足如实记于下表 |
| 四模式 | camera/world×nominal/bounded各自产profile与验证，不能跨域借用 | 四域calibrate/verify/replay均退出0且有速度，跨域借用profile负例退出1并命名报错 |
| 异CWD/错误输入 | 配置可加载，空输入/错schema/坏参数非零退出，无资格升级 | /tmp检查退出0；11个CLI负例退出1且原因匹配；显式零时间界退出0；所有合成回放G5资格0 |
| 原G2回放 | 所有帧含空mark均推进；缺Unique时退出2，不能冒充速度验收 | 原1676帧/raw MARK=1088/Unique=0；G4推进1676 tick、实测/速度/资格0，退出2；输入及相邻summary hash归档 |
| G2真实测量链 | G2预算关卡关闭后另跑真实Unique/误差/连续性；当前为阻塞 | G-01保持OPEN；BudgetMissing及两个空预算保持，不取候选补测量 |

建议目标与必须项分开：在同等数据/算力/门控口径下，KF降低速度尾部误差、反转恢复及时、可用覆盖不靠删失败；若证据不支持，保持研究配置而不发布硬件profile。不得为了追求100%统计表现放宽门槛。

## 踩坑记录

| 现象 | 原因 | 修改或保持 | 验证 |
|---|---|---|---|
| G2当前BudgetMissing | 面积预算为空但发布门控要求三项 | 本轮保持G2冻结，前置关卡独立审批 | 原回放诊断 |
| bounded_only缺profile时间界 | 原validator允许空optional、调用裸.value() | 按用户审查补G4_PROFILE_BOUNDED_TIME_BOUND_MISSING并移除裸.value()；有限非负语义不变 | 验证/构造主动断言及空值/缺key/负数/NaN/零界CLI均验证 |

## 已知限制

| 尚未证明 | 关闭条件 |
|---|---|
| 当前G2真实Unique链 | 独立批准G2预算语义/数据，原回归不退化 |
| 真实R/Q/关联门控/误差包含 | 受控独立真值、多会话、随机反转、不同距离/光照/噪声，按域重标定 |
| 曝光/时钟/执行延迟 | SDK与硬件实测，明确绝对界/统计来源，不以接收时间代替曝光 |
| 世界系速度 | 同曝光ego变换/协方差、安装/时钟一致、轮滑与姿态有效性 |
| 物理实例身份 | 多目标需求另设关联策略；当前只能几何连续区间 |
| 框口/角速度/控制/权限/RFID | G5/其他板块承担，G4不越界 |

实测更新：2026-11-01T15:33:45+08:00。初始基线至记录自然历时17.69分钟（含中断），恢复后10.32分钟；验收命令累计195.305s。各阶段实际命令耗时/退出码见[commands.jsonl](evidence/g4/commands.jsonl)及[timing.json](evidence/g4/timing.json)。软件施工与离线流程完成；剩余三关保持OPEN，当前不需修改算法或冻结G2的决策。没有实车证据，不写“实车G4验收通过”。

## 关卡当前状态

G-01=OPEN；G-02=OPEN；G-03=OPEN。关闭步骤与标准见[g4.md后续关闭流程](g4.md#后续关闭流程)。软件验收与硬件资格分开记录，不能因29/29而改成CLOSED。

## 本次内存检查与跨域探针

ASan/UBSan构建退出0。首轮沙箱CTest退出8，原因是LeakSanitizer不支持ptrace；按环境要求经授权在沙箱外重跑同构建、同参数（detect_leaks=1、halt_on_error=1），2/2通过，退出0且无ASan/UBSan/泄漏报告。两轮日志均保留，未关闭泄漏检查。[执行说明](evidence/g4/memory-check-note.md)。

33ms独立种子输入5458帧，沿用冻结14ms profile，速度/共同样本=0，退出1且原因匹配；profile前后SHA256相同。51个注入中47个被当未确认新测量seed、已确认错误接受=0；该跨域关联失败保留，不调整max_dt或增加门控。

## 四域独立holdout实测

|域|holdout帧|坏量测拒绝|速度样本|速度RMSE(m/s)|最大速度误差(m/s)|共同样本|反转恢复|
|---|---:|---:|---:|---:|---:|---:|---:|
|camera_relative-nominal_research|12849|128/128|12611|0.057740984|0.157927057|11757|97/98|
|camera_relative-bounded_only|12849|128/128|12481|0.069801801|0.181244581|11634|98/98|
|local_world-nominal_research|12849|128/128|12451|0.064506477|0.162762460|11601|96/98|
|local_world-bounded_only|12849|128/128|12381|0.077786546|0.192558883|11535|97/98|

|域|MEASURED速度3σ球包含/样本|COASTING速度3σ球包含/样本|
|---|---:|---:|
|camera_relative-nominal_research|10468/11445 (91.46%)|878/1166 (75.30%)|
|camera_relative-bounded_only|9318/11326 (82.27%)|738/1155 (63.90%)|
|local_world-nominal_research|10045/11306 (88.85%)|767/1145 (66.99%)|
|local_world-bounded_only|8127/11271 (72.11%)|632/1110 (56.94%)|

经验速度包含率仍不足，不按已见holdout改P/门槛，G-02保持OPEN。各域位置包含率、raw/一阶/KF全部及共同样本、反转恢复和逐帧失败完整见verification.md/holdout_errors.csv。

## 首个holdout会话回放与耗时

|域|帧|MEASURED|COASTING|LOST|速度帧|step均值(μs)|step p95(μs)|
|---|---:|---:|---:|---:|---:|---:|---:|
|camera_relative-nominal_research|4287|3903|361|23|4211|16.571|18.699|
|camera_relative-bounded_only|4287|3904|358|25|4188|18.191|20.625|
|local_world-nominal_research|4287|3906|355|26|4179|22.068|30.162|
|local_world-bounded_only|4287|3912|354|21|4179|19.812|23.848|

四域输入拒绝/G5资格均为0；step耗时仅含tracker.step，不含解析、写盘或上游，硬件/源完整性资格未授予。

证据目录及最终ZIP见[evidence/g4/INDEX.md](evidence/g4/INDEX.md)，包含全session输入/truth、源码快照与完整命令日志，不含build或第三方库。
