# Block4输出稳定层验收（已完成，批准范围内）

> 历史验收记录，代码和工具按 Block4 提交 `9b68daa` 复现。

2026-10-06。**Block4实现及本方案强制验收已完成（限已批准支持范围）。** 基线main与目标feat/temporal-stability均08c5c447348f46b9d033b90f109d48650745b4c6。G-B现已批准r=2px、偏离=2px并写生产；缺预算NOT_READY仍有负例覆盖。审批前raw非回归及批准配置正式稳定全视频均已通过，结果保留两批独立记录。

## 完成内容

保留core/config/preprocess/geometry/corners/pipeline六模块；新增稳定组件全在pipeline，共享FrameStamp在core，几何排序接口在corners，工具记录不进入算法库。冻结decode_stage无状态且未改；process/temporal_audit共享finalizeDecodedFrame，工具每帧只decode一次，生产文字源恒null。Detection/FrameResult直接字段和Detector四个调用签名、特殊成员签名未改，DisplayState仅按G-ABI追加value。

时间权重由14ms/.7构造tau，逐调用实际dt；double历史按当前原始slot保存，发布float再次检查再排序。选择参考是原始测量，平滑历史/显示历史独立。无/零/多关联均保留当前合法检测，最大四点面积/完全等面积输入顺序选单track。方向未知只用原始对原始循环误差区间，不回填方向；稳定输出复制当前载荷，仅替换点/bbox及当前方向排序映射。

empty立即无track，选择参考最多50ms且empty不续期；恢复首帧原始；零dt、歧义、非法几何/偏离回退原始并重建。非法图像/序列清选择、平滑、显示、输入元数据，下一合法低id首帧可重起；尺寸变化清历史。同尺寸换源/循环由调用方reset(InputChanged)。独立文字桥接按有效调用次数，第1～5缺失hold、第6清空，关闭透传不缓存。

D16：修复旧非法输入/序列return留下历史的问题，公共入口统一reset，新增I05及共享三状态恢复反例。NOT_READY清测量历史，同时保留合法输入帧戳作下一次序列检查，以保留旧重复帧负例，不缓存测量。D18：decode_audit仅改public_status=NOT_EVALUATED及说明，原始payload格式/链路保持；旧日志不回写。

CMake替换间接git rev-parse为MARK_COMMIT_LABEL，默认UNSPECIFIED；标签08c5c44仅表示基线，工作树真实源hash单独记录。新增3普通CTest，总19；Block5窗口/绘制/计时开关仍严格拒绝。G-B预算缺失可加载审计，开启内部稳定实例缺预算构造拒绝；生产关闭平滑仍不能绕过G-B。

## 决策记录

### G-ABI（已批准并落实）

批准依据为2026-10-06本方案§1.2/§4.3所载用户选A：“给DisplayState追加std::string value”；保持source_frame_id、age、is_held字段名称/类型/顺序，在后追加value。**这是ABI例外，FrameResult嵌套布局变化，不能混用旧ABI二进制。** 本轮mark_detector、app、旧16+新3测试、decode_audit/temporal_audit全部Release重新构建；独立H runner重新链接新库。类型见include/mark/detector_types.hpp，桥接见pipeline/display_history。没有重新请求既有批准。

### G-C（2026-10-06已确认，本期不实现）

**用户已确认：Block4不计算或启用Detection.confidence，生产继续null；v0.1保持二值输出。v0.2另立“置信度标定”需求，经完整流程批准后再启用。** 不给null改填0/1，不给残差改名概率，不将quality_flags当可比较置信分数。

理由：

1. `decode_stage.cpp:48–51`在已取到有效角的同时，仍有不能排除的竞争解释，因此返回NOT_DETECTED。稳定层看不到一个可合法发布的Detection；增confidence不是解决身份/证据缺失的办法。
2. 没有用户确认的V/N/U/O与人工角真值，无法标定“正确几何/正确方向”概率。H720证明固定合成支持条件，不是实拍概率标定集。
3. 冻结规定不可信empty、同几何异方向unknown、历史不补点/方向。用低分发布被硬拒绝的118帧是准入规则变更，超出输出稳定层。
4. 当前单track最大面积/单一关联路径不需要置信评分。本期平局按稳定候选顺序，不为排序捏造分数。

v0.2启用条件按以下七步独立执行，本期只记录、不实现：

1. **用户盲标**：在不看待评估检测输出的情况下标正例、负例、歧义与可测角点，保留标注不确定性。
2. **定义事件**：明确置信度代表何事，例如完整四点是否都在批准定位预算内；方向正确另定义，不混作一个含糊“质量”。
3. **拆分数据集**：按视频/片段分训练或拟合集、校准集、独立测试集，不随机打散相邻帧伪独立。
4. **确定公式**：明确单帧特征、函数、输出含义及失败值；先审公式和所在层，再实现，不将残差直接命名概率。
5. **标定**：只在标定数据上拟合参数及概率映射，保存数据版本/参数来源。
6. **验证**：在独立测试集报告可靠性曲线、Brier分数、覆盖/拒绝率、几何与方向错误，披露失效条件。
7. **审批门槛**：用户批准指标、启用范围与低分处理后才启用；竞争路径准入如需变更另作明确决议。


以上四条原因与七步为独立v0.2需求；本期没有新增评分函数/参数、confidence保持null，不以quality_flags当分数，不用历史软放行118个竞争拒绝帧，v0.1封存未改。

### G-B（2026-10-06已批准并落地）

审批前两值未批准，生产节点省略；实验r候选2px、偏离0.5/1/2/4px的代价完整保留在[预算表](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/docs/block4_budget.md)。用户本轮答复原文：“**批准 r=2、偏离=2（推荐）**”，问题明确范围限固定合成条件与行为验证，不扩展一般仿射/透视/距离/光照保证。收到并记录时间2026-10-06T10:47:48.462010+08:00，单位原图px，算法Block4-v1，数据C/H block3-v1 frozen v7及固定480×32语义网格。[原文JSON](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/G-B-approval.json)归档。已同步生产YAML两数值，稳定默认开启、显示桥接默认关闭；加载/校验/导出代码与主动测试已实现，批准后再次跑Release19/19、相关Debug5/5通过。正式全视频稳定审计及逐帧独立核查已通过，结果在下方追加。缺预算或任一预算缺失仍NOT_READY，即使关闭平滑也不能绕过。

## 验证方法与结果

Linux GNU13.3/CMake3.28.3/OpenCV4.6.0/OpenSSL3.0.13，C++17，无新依赖/测试框架。Release构建/CTest退出0，19/19通过；T01～T18（含T10b）19个命名用例，H01～H05五项，I01～I06六项，共30组主动用例均PASS。每组含多个断言，30是组数，不冒称断言数量。Debug对新三测试及配置负例/往返批准后再次5/5通过（覆盖最终共享非法反例）。旧assert/空哨兵覆盖限制仍披露。

| 用例 | 实际输入/预期 | 执行结果与证据 |
|---|---|---|
| T01 公式 | computeTimeConstant(14,.7)，dt=.007/.014/.028 / tau等于公式，alpha约.452277/.7/.91；double误差≤1e-12；不把tau近似常数写入实现 | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| T02 非法参数 | dt参考0/负/NaN/Inf，alpha0/1/NaN，负hold，门限NaN，缺两预算之一 / 非法配置拒；缺预算非空配置按就绪规则，开启update实例不默默降级；往返不丢字段 | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| T03 首帧/连续 | t=0,Q；t=14000,Q+(1,0)，两帧方向可信 / 首帧原始；第二帧稳定Q+(.7,0)，原始集合仍Q+(1,0)，bbox由稳定点重算；float每点误差≤1e-5 | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| T04 实际Δt | 重置后Q，再t=7000或28000,Q+(1,0) / 稳定位移对应.452277或.91，不用固定.7 | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| T05 Δt=0 | Q@0，Q+(1,0)@0，下一帧再移动 / 第二帧原始、alpha=null、ZERO_DT；第三帧使用第二帧重建历史 | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| T06 有效→empty | Q@0，空@14000 / 当前detections和tracks都空，平滑失效，不能出现历史点；未超期选择参考可留 | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| T07 恢复 | Q@0，空@14000，Q+(2,0)@28000，Q+(3,0)@42000 / 恢复帧原始2；下一帧2.7；不从Q跨空段滤成1.4 | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| T08 已知→未知 | 已知Q，再当前orientation=null的平移Q / 当前和稳定orientation均空；几何可以稳定；历史已知不回填 | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| T09 分界换位 | 连续菱形跨排序分界，物理坐标由固定旋转矩阵生成，已知orientation随当前排序置换 / 按同物理角计算golden滤波，输出再次排序；三帧以上验证内部slot历史没有错位，raw始终不改 | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| T10 未知循环 | Q绕中心(150,150)先旋转44°、后46°，分别正确屏幕排序且orientation=null，r fixture=2 / 纯对应helper及update选正确循环；按实际同角轨迹计算golden，稳定点不拉向邻角，输出方向仍null；不能拿未按LT排序的伪Detection测试 | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| T10b 对应歧义 | Q@0后绕(150,150)旋转45°@14000，两帧正确排序、orientation=null，r fixture=2 / 两循环误差区间重叠；原始回退、CORRESPONDENCE_AMBIGUOUS、重建历史；不得选择第一个最小值冒称唯一 | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| T11 关联失败 | Q后输入中心距>0.5D的可信方框 / 当前原始track仍存在，matched_history=false；不删除Detection | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| T12 多候选 | 无历史大小不同/完全等面积；有历史恰1、0、2项落门限 / 大面积/稳定顺序，单track引用合法索引；多个可能匹配则歧义重选，raw集合全保留 | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| T13 门限边界 | 中心距恰0.5D、面积比恰.5或2；历史gap=50000与50001 / 三门限包含等号；gap50000可参考，50001过期原始；不增加epsilon宽松量 | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| T14 平滑非法 | temporal_geometry直接输入非凸/自交/越界/重复/NaN；update用Q@0、Q+(10,0)@14000且偏离上限fixture=2，再Q+(11,0)@28000 / helper明确失败；第二帧候选平滑滞后3px超过2，输出可信当前原始track并重建历史；第三帧输出Q+(10.7,0)，不裁点/不借证据伪通过 | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| T15 时间/帧号非法 | 已有历史后重复id、倒退id、倒退time、负time、非法source枚举 / INVALID_INPUT、三状态清；下一合法低id/新段首帧原始；不能只reset平滑 | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| T16 换源/循环/尺寸 | Q后reset(InputChanged)、frame_id从0；同尺寸换源必须调用reset；尺寸变化 / 新段原始首帧，显示历史清；测试/README明确外部调用责任 | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| T17 实例隔离 | A/B交错喂不同轨迹，其中仅A reset；与各自单独运行比较 / 每帧坐标/方向/原因与单独运行一致，B不受A影响 | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| T18 关闭平滑 | stabilization_enabled=0，连续移动与失检 / 当前选择track原始透传，empty仍空；不应用滤波；正式完成验收另测开启 | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| H01 5/6桥接 | A@frame0，连续6次null，hold=5开启 / A当前held=false/age0；1～5 A原来源/age调用次数；6 null；有效点/track不受影响 | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| H02 首次缺失/零hold | 从未A先null；另A后首个null且hold=0 / 两者null，无伪造文字 | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| H03 新值替换 | A，null，B，null / B立即held=false/age0，新来源；之后只hold B | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| H04 跳号 | A@id0，null@id100，null@id200 / age1、2，不是100、200 | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| H05 关闭/非法/reset | 关闭A→null；开启A后非法stamp/reset / 关闭不保存历史；非法/reset立即null；后续缺失仍null | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| I01 公共状态 | 缺assignment或corner预算；开启缺G-B；完整配置黑帧；完整合成MARK / 分别NOT_READY/NOT_READY/NOT_DETECTED/DETECTED；正常最多1 track，空时0，metadata准确 | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| I02 层间隔离 | 当前empty；或当前unknown而历史已知；显示独立合成测试 / empty无有效点/track，unknown不回填；生产不注入假文字，桥接单测不改有效输出 | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| I03 raw/稳定一致 | 同一真合成帧经runDecodePipeline与process / process.detections字段逐一等于decode；稳定结果仅允许点/bbox/当前方向排序变化；confidence/marker_code仍null | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| I04 原图/当前引用 | 合成目标放原图右下超过工作图边界；多候选fixture选择非0索引 / 坐标不误判工作size，track.detection_index指向本帧正确Detection；稳定bbox逐点min/max | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| I05 公共非法清理 | 先建立Detection/track，再非法图像/序列，最后合法新帧 / INVALID_INPUT且载荷空，新帧原始；不能保留旧显示/位置 | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |
| I06 旧排序等价 | 原screen_order所有样例与旋转0～345°15°步长、平局/非有限样例 / 共享核心抽取前后P0～P3映射/屏幕点/tie/失败完全一致；未知环新接口只产生slot映射 | PASS；[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/active-cases.log) |

基本fixture方框Q(100,100)～(200,200)、原图1440×1080、14ms/.7/50ms、r2/偏离20（T14偏离2）只是机制值。旋转物理golden由固定矩阵独立计算；T09四点历史double保留，T10合法LT排序后按物理golden比较；T10b45°区间重叠不能选最小者。I01/I03/I04/I05使用真实渲染MARK→真实preprocess/geometry/decode→公共process；不向正式Detector传真值。I06及旧screen_order七个golden样例逐项查新接口等价，原算法能量/平局核心原样保留。

配置负例逐字段检查NaN/Inf/0/负值，alpha1、面积比错误、负hold、两预算缺失构造；YAML错误类型/空字符串/重复/未知、浮点hold与溢出；旧schema1冻结起点省略兼容、旧三字段缺失拒绝；往返使用非默认值覆盖全部新增字段。CLI缺参数、已有输出拒绝、视频打不开三项均退出1，见[audit负例](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/audit-negative-tests.json)。

```bash
cmake -S src/tushenghao -B build/block4 -DCMAKE_BUILD_TYPE=Release -DMARK_COMMIT_LABEL=08c5c44
cmake --build build/block4 -j 4
ctest --test-dir build/block4 --output-on-failure
cmake -S src/tushenghao/tools/block3_fixture -B build/block4-fixture -DCMAKE_BUILD_TYPE=Release -DMARK_COMMIT_LABEL=08c5c44
cmake --build build/block4-fixture --target block3_verify -j 4
build/block4-fixture/block3_verify build/block3-H-v1 src/tushenghao/docs/evidence/block3/frozen_detector_v7.yaml build/block4-evidence/H-regression.jsonl
build/block4/temporal_audit --video data/raw/marker_video.avi --config src/tushenghao/config/detector.yaml --output build/block4-evidence/temporal-production-pending.jsonl
python3 build/block4-evidence/compare_regression.py
```

H重新执行720/720正确、isolated720/720、wrong_valid0、退出0；与main基线整理记录逐case_work_id比对status/detections/measurements/diagnostics/truncated及correct标志无差异，见[H对照](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/H-comparison.json)。未重建任何图片/海量网格。原720通过ID全部保留。

全视频0～1675、SHA256 aa1219a7a7b702ea1265be8752853a267c982a651afa0f7846f516f9517c9ac7；fps产生时间，1676/1676记录、raw864/empty812，无raw状态/角点/bbox/方向/证据/原因/截断差异，confidence/marker_code空。该批当时生产G-B缺失，最终1676帧全NOT_READY、detections/tracks空、display=null；不是批准配置稳定视频回归。见[video对照](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/video-comparison.json)。该视频二进制源hash为记录中的实际hash，完成后又增补公共尺寸原因及测试等价检查；几何/decode生产源未改，不将基线标签当最终源码提交。排除标签/源与配置hash/耗时元数据，不排除任何raw几何或原因字段。

480段/15,360调用固定语义实验：合法测量误删0、错对应0、unknown回填0、raw改写0；每档偏离/滞后/回退分项完整见预算表及CSV/JSONL。不把运动算抖动，不以视频检出数作准确率。审批前正式稳定验收曾为BLOCKED_G-B_PENDING；本轮收到批准后已执行并通过，见追加结果。

### G-B批准后正式稳定全视频（PASS）

正常配置稳定开启、桥接关闭，0～1675全1,676帧，raw检测864、empty812，公共共享装配与decode状态一致；当前detections逐字段完全保留基线，原始观测证据/方向/框/原因/截断逐帧无差异。所有864检测帧恰1个track引用合法当前索引，812个empty帧track=0，生产display恒null，confidence/marker_code恒null。

平滑实际应用182帧（864的21.06%）；其余682原始回退，分项NO_HISTORY=8、SMOOTHING_DEVIATION=499、SMOOTHING_HISTORY_EMPTY=169、HISTORY_EXPIRED=6。首帧/失检恢复共183次均原始，未跨失检滤点。499个偏离回退如实保留，不放宽2px预算换更多平滑。相对当前点偏离均值0.222161px、P99=1.795913px、最大1.994915px≤批准2px；每track检查有限/正凸/非自交/画内/float发布/bbox逐点minmax。独立Python从审计对应映射和当前raw递推double公式，再验证float发布及方向排序，无错误物理或非法循环映射，无误删当前测量、历史点泄漏或崩溃。视频全部864输出方向已知，unknown不回填由T08/T10/I02主动反例验收，不把视频的零unknown称作该路径覆盖。

命令及退出码：temporal_audit（批准配置）0；compare_approved.py 0；批准后最终Release CTest19/19、0，相关Debug5/5、0。[正式逐帧JSONL](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/temporal-approved.jsonl)、[独立核查JSON](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/approved-comparison.json)、[视频日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/video-approved.log)、[Release最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/ctest-delivery.log)、[Debug最终日志](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/ctest-debug-approved.log)。独立文字合成5/6边界实际来源/年龄/held见[display-synthetic.jsonl](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/display-synthetic.jsonl)，不与生产显示记录混合。

```bash
build/block4/temporal_audit --video data/raw/marker_video.avi --config src/tushenghao/config/detector.yaml --output build/block4-evidence/temporal-approved.jsonl
python3 build/block4-evidence/compare_approved.py
```

正式记录实际源码SHA256：`a81b6a729e14f0db67fa1a0be60e6c106c83d6e95d0f0db8bee304b3f44b0931`，配置SHA256：`1a093a6fa6d36bbe4efdfdac8f3f2d1086eb4808f01833982ebde2816e81d50a`，有效配置SHA256：`6a76ecbc64f877281eaac3be4c4603357d4bfaba00d6db6c529edb7dfaaad404`；批准记录SHA256：`38c1de0854affebc0f9e4a41832fa7f1a8aace9b5ea44e61b770a72b439d837c`。budget_version字符串提示配置数值需要外部批准依据，以本页原文/配置hash与批准JSON关联；不由工具文本自动声称人类已批准。基线标签08c5c44不是当前未提交源码版本。正式视频完成后未修改生产算法/配置，仅更新文档/交付证据。

未标注视频，864不是真值准确率/召回；本次验收是raw非回归和稳定行为安全。没有实拍真值，不宣称真实视频RMSE或视觉收益；静止收益与运动滞后只来自预设合成对照。性能14ms仍未验收。

## 踩坑记录

首次构建失败：新screen_order.hpp缺OpenCV类型声明；补直接include，不借传递include隐藏依赖。首轮CTest17/19：T13中心等号失败因为hypot(float,float)返回float，改明确double运算，不加epsilon放宽门限；hold4294967296被OpenCV截断为0，节点读取无法恢复原值，在新增hold字段原始YAML十进制token补溢出检查，旧geometry/corner规则未改。最初平滑发布float同时覆盖内部double历史，复核后改为独立published副本；double原始slot缓存不受对外排序/float影响。旧失败日志保留，不删除换绿。

第0步读到禁Git约束前已执行只读git status/log（未修改Git），这是执行边界偏离；发现后立即停止Git，改读.git/HEAD与refs确认main/目标一致。后续没有执行Git、提交、推送或切分支，CMake移除间接Git。此偏离如实保留，不声称全程零Git。基线只读hash证据见[baseline.json](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/baseline.json)。

## 已知限制与未闭合问题

1. G-B数值已批准及落地，正式全视频稳定验收通过；本方案Block4强制项无剩余阻塞。视频人工真值/概率/性能等未授权范围不计作本期完成项。
2. D23原三L投影/观测面积规则和D25两份模型角边硬绑定按本方案范围外保留；本期不改geometry/preprocess/其余corners，不新增第三份绑定。
3. D12既有证据检查不重新fitLine验证L2最优或复验图像来源；稳定不伪造证据，不宣称已完成全部防篡改性质。
4. 一般仿射、短边、任意远距离/透视/光照覆盖不足；r统计支持范围见预算，未知方向快速旋转不保证持久身份。V/Q及置信度独立需求不因本期通过而闭合。
5. 旧其他assert与config_contract空哨兵不计实质覆盖；Block3整体仍WIP，保留历史。性能14ms未验收，Block5计时/绘制未实施，不能由稳定层宣称改善约97.65ms旧阶段性能。
6. 参考和实验脚本/大数据位于build/block4-evidence，删除build前须归档；必需文档及所有新源码导航已登记，旧冻结文档/evidence/v0.1未回写。

## 实际耗时与剩余动作

实际事件采用文件/log时间戳（Asia/Shanghai）：第0步基线快照09:29:42，源码骨架09:30:55，首次configure09:36:57，首轮CTest09:37:54，修复后09:38:34；实验完成09:41:07；H完成09:40:53、审批前视频09:42:22；首轮交付测试09:43:07、文档09:46:52。09:46:52～10:47:07之间没有代码/验证产物事件，是会话间隔，**不计入主动耗时**。G-B收到并归档10:47:48；批准后测试10:49:10/14。以下实施步骤交错执行，主动时间按有产物的活动区间估计，不声称有独立秒级计时器；冻结工时估计未用于删范围。

| 步骤 | 开始/结束（2026-10-06） | 主动耗时口径 | 验证命令/退出码 | 剩余项 | 阻塞/批准依据 |
|---|---|---|---|---|---|
| 0 基线 | 开工～09:29:42快照 | 阅读约数分钟，精确开始未计时 | 直接读HEAD/refs、baseline/0 | 无 | main/目标同08c5c44 |
| 1 骨架 | 09:29:42～09:30:55 | 约1.2min | 随Release全部构建/0 | 无 | G-ABI直接批准落地 |
| 2 配置/公式 | 09:30:55～09:38:34 | 与3/4/6/7交错，共约7.7min，不重复累加 | configure/0、首轮失败保留、修复后CTest/0 | 无 | schema1兼容，Block5开关仍拒 |
| 3 选择/失检 | 同上 | 包含在交错活动区间 | T03～T08/T11～T18/0 | 无 | 冻结50ms及中心/面积门限 |
| 4 对应/稳定 | 同上；double历史复核至09:40:13 | 已包含相关活动区间 | T09/T10/T10b/T14/I06/0 | 无 | r/偏离先fixture，现G-B获批 |
| 5 实验/审批 | 09:39:15～09:41:07；10:47:48批准 | 实验分析约1.9min，会话等待不计 | measure/grid/summarize/0 | 无（正式视频已PASS） | 用户明确批准2/2及范围 |
| 6 独立桥接 | 骨架09:30:55，H测试09:37:54/10:49:10 | 包含在实施和测试区间 | H01～H05及合成JSONL/0 | 无 | 5/6调用边界已冻结 |
| 7 公共装配 | 09:30:55～09:38:34；批准落地10:47:48～10:49:14 | 与实现交错；批准落地约1.5min | I01～I06、Release19/19/0、Debug5/5/0 | 无（正式视频已PASS） | D16全清，缺预算负例保留 |
| 8 审计/文档 | 工具09:36:57；回归09:40～09:42；文档09:43～09:46:52；批准后继续 | 文档约3.8min+回归运行时间，交错不重复算 | H720/0、pending视频1676/0、raw比对/0 | 无（正式视频及交付检查已PASS） | 已获G-B，可正常稳定验收 |

精确产物时间戳及命令退出码见commands/artifact manifest；本轮主动区间不能用总会话墙钟（含约1小时会话间隔）冒充，尚无逐步骤独立CPU/人工操作计时。这个计时精度限制明确披露。

修改文件及所有产物实际hash/大小/可重放命令与退出码见[交付manifest](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/artifact-manifest.json)、[命令记录](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block4/commands.json)。人工批准记录待追加；本轮不操作版本管理。

最终核查：截至2026-10-06T10:51:51.354904+08:00，正式视频与独立核查退出0；必需两份Block4文档、README和docs/tools索引齐全、相对链接可解析。geometry/preprocess/decode_stage及其余corners锁定源29份hash一致，无新增Git调用。完整修改文件表见下方和交付manifest。

### 实际修改文件

| 类别 | 文件 |
|---|---|
| 修改既有 | [CMakeLists.txt](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/CMakeLists.txt) |
| 修改既有 | [detector.yaml](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/config/detector.yaml) |
| 修改既有 | [detector.hpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/include/mark/detector.hpp) |
| 修改既有 | [detector_config.hpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/include/mark/detector_config.hpp) |
| 修改既有 | [detector_types.hpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/include/mark/detector_types.hpp) |
| 修改既有 | [config.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/lib/config/config.cpp) |
| 修改既有 | [screen_order.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/lib/corners/screen_order.cpp) |
| 修改既有 | [detector.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/lib/pipeline/detector.cpp) |
| 修改既有 | [block3_contract_regression_test.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/tests/block3_contract_regression_test.cpp) |
| 修改既有 | [config_error_test.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/tests/config_error_test.cpp) |
| 修改既有 | [config_roundtrip_test.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/tests/config_roundtrip_test.cpp) |
| 修改既有 | [screen_order_test.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/tests/screen_order_test.cpp) |
| 修改既有 | [decode_audit.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/tools/audit/decode_audit.cpp) |
| 新增实现/测试/工具 | [temporal_fixture.hpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/test_support/temporal_fixture.hpp) |
| 新增实现/测试/工具 | [temporal_stabilizer_test.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/tests/temporal_stabilizer_test.cpp) |
| 新增实现/测试/工具 | [display_history_test.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/tests/display_history_test.cpp) |
| 新增实现/测试/工具 | [temporal_integration_test.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/tests/temporal_integration_test.cpp) |
| 新增实现/测试/工具 | [frame_stamp.hpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/lib/core/frame_stamp.hpp) |
| 新增实现/测试/工具 | [screen_order.hpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/lib/corners/screen_order.hpp) |
| 新增实现/测试/工具 | [display_history.hpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/lib/pipeline/display_history.hpp) |
| 新增实现/测试/工具 | [display_history.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/lib/pipeline/display_history.cpp) |
| 新增实现/测试/工具 | [temporal_stabilizer.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/lib/pipeline/temporal_stabilizer.cpp) |
| 新增实现/测试/工具 | [temporal_geometry.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/lib/pipeline/temporal_geometry.cpp) |
| 新增实现/测试/工具 | [temporal_geometry.hpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/lib/pipeline/temporal_geometry.hpp) |
| 新增实现/测试/工具 | [temporal_stabilizer.hpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/lib/pipeline/temporal_stabilizer.hpp) |
| 新增实现/测试/工具 | [frame_sequence.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/lib/pipeline/frame_sequence.cpp) |
| 新增实现/测试/工具 | [stabilize_stage.hpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/lib/pipeline/stabilize_stage.hpp) |
| 新增实现/测试/工具 | [stabilize_stage.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/lib/pipeline/stabilize_stage.cpp) |
| 新增实现/测试/工具 | [temporal_types.hpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/lib/pipeline/temporal_types.hpp) |
| 新增实现/测试/工具 | [frame_sequence.hpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/lib/pipeline/frame_sequence.hpp) |
| 新增实现/测试/工具 | [temporal_record.hpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/tools/common/temporal_record.hpp) |
| 新增实现/测试/工具 | [temporal_audit.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/tools/audit/temporal_audit.cpp) |
| 文档/导航 | [README.md](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/README.md) |
| 文档/导航 | [INDEX.md](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/docs/INDEX.md) |
| 文档/导航 | [INDEX.md](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/tools/INDEX.md) |
| 文档/导航 | [block4_acceptance.md](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/docs/block4_acceptance.md) |
| 文档/导航 | [block4_budget.md](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/9b68daa/src/tushenghao/docs/block4_budget.md) |
