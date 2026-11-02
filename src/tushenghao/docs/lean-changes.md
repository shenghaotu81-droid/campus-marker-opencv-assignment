# 瘦身修改报告

本报告依据 `refactor/lean` 的实际提交和本机验证编写。代码收尾提交：`4c00708658a49600e29c3b00478cb4da1cdb4061`；基线：`7a7ef662cf348589aea8d43251d1b2aaf671f45d`。只读的 [lean-report.md](lean-report.md) 与 [move-layout-draft.md](move-layout-draft.md) 未改。

71 项中，67 项已落地，F50/F52 按裁决部分落地并保留搬家项，F25/F65 按要求不改。没有移动／重命名文件，没有新建模块 CMakeLists 或拆顶层 CMake。必要共用函数使用普通值／const 引用／显式参数；新增五个小私有／测试能力头，没有插件框架、注册表或每步一类。

生产算法阈值、空预算和三关状态保持：**G-01/G-02/G-03 均 OPEN**。G4 公开输出与资格语义冻结；合成标定／回放不能当作实车确认。配置协议迁移后仍保留 schema_version=1，旧键／旧模式明确拒绝；角证据子版本为 2，外层 FrameRecord 仍为版本 1。

## 已完成批次

| 提交 | 实际批次 |
|---|---|
| `db74cdf` | [lean] 修复证据往返、ID关联及标定与汇总坏输入处理 |
| `c94252f` | [lean] 删除六项可证冗余分支、死代码与旧包装 |
| `a450cae` | [lean] 统一角身份与模块校验并隔离相机配置快照 |
| `6163099` | [lean] 合并迁移显示配置并删除无效旋钮及旧模式名 |
| `3baeb48` | [lean] 显式传递主链阶段产物、执行范围和运行结论 |
| `abec399` | [lean] 解耦标定样本身份、合成步骤声明与验证驱动输入 |
| `f87de8e` | [lean] 删除失效入口并统一几何度量、记录命名和测试驱动 |
| `8604243` | [lean] 复用G4同状态创新并仅复制可变事务状态 |
| `acdf1e9` | [lean] 复用几何计量与角点排序并压平十三路回退 |
| `4c00708` | [lean] 统一严格输入与原因编码并按实际目标记录构建身份 |

每批一个 `[lean]` 提交并推送；本报告和流程导读各自独立提交在代码批次之后。

## 按 F 编号的实际改动

以下标题保留原报告的问题含义；“实际”描述收尾后的代码。每条给出当前文件:行，已删除文件在正文明确标注，不伪造当前行号。最终默认／全量／合成测试的完整命令与日志见附录。

### F01 · 完成 · 原问题：物理角身份在三个步骤分别硬编码

步骤：`D11/P01/Q04`。定位：[lib/core/marker_geometry.hpp:20](../lib/core/marker_geometry.hpp#L20)；[lib/corners/corner_resolver.cpp:74](../lib/corners/corner_resolver.cpp#L74)；[lib/corners/corner_evidence_validation.cpp:34](../lib/corners/corner_evidence_validation.cpp#L34)；[lib/config/pose_config.cpp:58](../lib/config/pose_config.cpp#L58)。

实际：物理角片段／顶点／邻边归一个只读 ModelCornerContract；resolver、证据校验、G2 米制取点共用身份，不共用拟合结果。

验证：corner_resolver_test、block3_contract_regression_test、pose_contract_test；全帧及 G4 对照通过。

裁决／偏离：原报告提议由加载器产出表；本次保留固定图纸身份常量，不新增可变配置或模型准入门控。

### F02 · 完成 · 原问题：观察请求成为算法控制暗道

步骤：`D09/A05/A06`。定位：[lib/pipeline/decode_stage.hpp:34](../lib/pipeline/decode_stage.hpp#L34)；[lib/pipeline/decode_stage.cpp:282](../lib/pipeline/decode_stage.cpp#L282)。

实际：runDecodePipeline 显式接收 ExecutionScope；无 observer 同范围也执行同一算法，request.scope 只作观测标签。

验证：observability_integration_test 的 scopes、geometry_budget_isolation 及新增有／无 observer 指纹对照。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F03 · 完成 · 原问题：StageDetails 绑住阶段实现并复制阶段状态

步骤：`D02/D03/A06/A08`。定位：[lib/pipeline/decode_stage.hpp:21](../lib/pipeline/decode_stage.hpp#L21)；[app/debug_renderer.hpp:6](../app/debug_renderer.hpp#L6)；[lib/pipeline/diagnostics_context.hpp:10](../lib/pipeline/diagnostics_context.hpp#L10)。

实际：预处理、组件、观察、生成／验证／补全成为逐步产物；主链不回填 PreparedFrame.components_。渲染适配器只交付原图点线 RenderInput，实验 JSON 由工具编码。

验证：debug_renderer_test、diagnostics_record_test、observability_integration_test；1676 帧完整公开输出逐字相同。

裁决／偏离：保留 PreparedFrame.components_ 仅作旧独立探针适配；serializer 保留各步骤只读快照协议，不新建类框架或移动文件。

### F04 · 完成 · 原问题：模块构造绕到全应用配置校验

步骤：`Q03/T01/D01`。定位：[lib/config/config.cpp:498](../lib/config/config.cpp#L498)；[lib/pipeline/detector.cpp:38](../lib/pipeline/detector.cpp#L38)；[lib/pipeline/temporal_stabilizer.cpp:32](../lib/pipeline/temporal_stabilizer.cpp#L32)。

实际：校验拆成 Temporal／Detector／Observability 三个函数；总入口保留原顺序，构造器只调用自己的契约。

验证：config_error_test、config_roundtrip_test、temporal_stabilizer_test、detector_contract_test。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F05 · 完成 · 原问题：config 反向依赖私有 Kalman 实现

步骤：`Q05/K03`。定位：[lib/core/observed_geometry_utils.hpp:75](../lib/core/observed_geometry_utils.hpp#L75)；[lib/config/tracking_config.cpp:133](../lib/config/tracking_config.cpp#L133)；[lib/tracking/kalman_cv.hpp:44](../lib/tracking/kalman_cv.hpp#L44)。

实际：正定性数学原样归低层；tracking_config 不再包含 Kalman 实现头，filter 仍使用同一数学检查。

验证：tracking_contract_test、tracking_io_test；G4 四组合 profile／轨迹逐字一致。

裁决／偏离：使用既有低层头，不为布局新建 math 模块或移动源文件。

### F06 · 完成 · 原问题：报告为 JSON 引号反向包含记录器

步骤：`A07/A12/V03`。定位：[lib/pipeline/run_report.cpp:11](../lib/pipeline/run_report.cpp#L11)；[lib/pipeline/run_report.hpp:8](../lib/pipeline/run_report.hpp#L8)；[lib/pipeline/diagnostics_recorder.cpp:120](../lib/pipeline/diagnostics_recorder.cpp#L120)。

实际：JSON 转义只保留一份，归现有 run_report；报告与 recorder 显式使用，不相互包含生命周期实现。

验证：diagnostics_record_test、observability_integration_test；完整公开结果 JSON 与基线逐字相同。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F07 · 完成 · 原问题：运行结论经可写 summary 和字符串 map 传递

步骤：`A07/A11/A12`。定位：[lib/pipeline/run_report.hpp:19](../lib/pipeline/run_report.hpp#L19)；[lib/pipeline/diagnostics_recorder.cpp:755](../lib/pipeline/diagnostics_recorder.cpp#L755)；[app/run_console.cpp:117](../app/run_console.cpp#L117)。

实际：完成判断和外部开销成为 RunOutcome／RunOverhead，finishRun 显式消费；summary() 只读，console 不再解码 environment 特定键。

验证：diagnostics_record_test 生命周期／导出、observability_integration_test 完整性／终端／采样。

裁决／偏离：manifest 边界继续编码原 coverage/completion_reason 键，终端文字和退出语义不变。

### F08 · 完成 · 原问题：const config() 仍泄漏相机可变存储

步骤：`P01/P11/Q04`。定位：[lib/pose/pose_estimator.cpp:62](../lib/pose/pose_estimator.cpp#L62)；[include/mark/pose_estimator.hpp:31](../include/mark/pose_estimator.hpp#L31)。

实际：config() 改为按值深拷贝快照，K/dist clone；调用方不能通过 cv::Mat 浅别名修改估计器。

验证：pose_contract_test 新增快照矩阵改写后原实例不变，原位姿回归通过。

裁决／偏离：按六条定法正式改公开 getter；不保留 const 引用加隔离副本的双入口。

### F09 · 完成 · 原问题：旧图纸几何 helper 只被专属测试使用

步骤：`Q04`。定位：[CMakeLists.txt:62](../CMakeLists.txt#L62)。

实际：删除 core/geometry_utils.hpp/.cpp、geometry_utils_test.cpp 及登记；真实 observed_geometry_utils、模型合法性与观察验证保留。

验证：默认／全量构建及 geometry_model_test、geometry_anchor_evidence_test、detector_contract_test。

裁决／偏离：这组 helper 只有自身测试消费者；这是报告授权的一个无效测试目标删除。

### F10 · 完成 · 原问题：三个公开角点旋钮未被算法消费

步骤：`Q02/D10/D12`。定位：[tests/config_error_test.cpp:464](../tests/config_error_test.cpp#L464)；[include/mark/detector_config.hpp:112](../include/mark/detector_config.hpp#L112)；[lib/config/config.cpp:157](../lib/config/config.cpp#L157)。

实际：删除 local_search_margin_ratio、reject_truncated_corner、edge_point_distance_threshold 三个公开无效旋钮及读验写；旧键逐项拒绝，实际截断检查保留。

验证：全仓消费者检索；完整合法新配置注入旧键负例、roundtrip、block3_contract_regression_test。

裁决／偏离：与 F28/F31 一次迁移、同批验证。

### F11 · 完成 · 原问题：未测量 simplification_error_ 恒为零

步骤：`D04/A07`。定位：[lib/pipeline/diagnostics_recorder.cpp:356](../lib/pipeline/diagnostics_recorder.cpp#L356)。

实际：删除未测却恒零的内部成员／赋值；旧记录键保留为 null，表示 unknown。

验证：diagnostics_record_test 实际观察快照逐项核对 null；检测输出金样不变。

裁决／偏离：这是记录兼容适配，不新增误差门限。

### F12 · 完成 · 原问题：三 L 枚举与诊断仿射的名字混淆用途

步骤：`D05/V02`。定位：[lib/geometry/geometry_matcher.hpp:9](../lib/geometry/geometry_matcher.hpp#L9)。

实际：三 L 对应枚举改名 generateThreeLAssignments；首凹点仿射片段移入 parent_diagnose.cpp，名为 fitFirstConcaveAnchorAffine，生产声明删除。

验证：geometry_matcher_test、geometry_anchor_evidence_test；parent 工具构建及既有稀疏／重排 ID 探针。

裁决／偏离：移动函数片段，不移动或重命名文件；完整实际锚点元组拟合保留。

### F13 · 完成 · 原问题：ID 查找在同帧反复线性遍历

步骤：`D06/D07/D08`。定位：[lib/core/geometry_types.hpp:193](../lib/core/geometry_types.hpp#L193)。

实际：按 ID 查观察／组件的只读函数归一处；legacy 未填来源只在该入口解释，生产与探针共用。

验证：geometry／assignment／corner 系回归，F44 稀疏／重排／重复／缺失 ID 场景，全帧对照。

裁决／偏离：保留原“所查 ID 重复”的异常优先级，不额外对无关重复记录做提前拒绝；不新增通用索引框架。

### F14 · 完成 · 原问题：同一绑定投影两次再分别验证

步骤：`D07`。定位：[lib/geometry/geometry_validation.cpp:98](../lib/geometry/geometry_validation.cpp#L98)；[lib/geometry/geometry_validation.cpp:156](../lib/geometry/geometry_validation.cpp#L156)。

实际：每绑定仅投影一次；残差先检查，面积规则消费同一投影面积／观测面积，完整性降级顺序保留。

验证：geometry_matcher_test、geometry_anchor_evidence_test、path_a_competition_test；1676 帧及合成金样。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F15 · 完成 · 原问题：快速面积筛查与详细计量重复

步骤：`D08`。定位：[lib/geometry/assignment_match_metrics.hpp:11](../lib/geometry/assignment_match_metrics.hpp#L11)；[lib/geometry/geometry_assignment_completion.cpp:21](../lib/geometry/geometry_assignment_completion.cpp#L21)。

实际：廉价面积预筛返回已测 AreaMetric，完整计量接收该值；独立七参入口仍自行计算并检查输入。

验证：assignment_completion_test，含已有绑定／拓扑／恰好耗尽；合成 25＋88 行逐字相同。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F16 · 完成 · 原问题：固定四候选之后的空集合兜底不可达

步骤：`D15`。定位：[lib/corners/screen_order.cpp:651](../lib/corners/screen_order.cpp#L651)。

实际：删除四个候选已构造后的不可达空集合分支；真实退化／非有限／平局检查不动。

验证：screen_order_test 及出版角度／时序回归，39 组 reference。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F17 · 完成 · 原问题：FastRecovery 会重试完全相同的 PhaseMedian 阶段

步骤：`D12/D13`。定位：[lib/corners/corner_edge_fit.cpp:750](../lib/corners/corner_edge_fit.cpp#L750)。

实际：13 路变为显式有序表；Fast 的第 8/9 逻辑路复用第 2/4 路同输入结果，并重放旧 stage 编号、弧及配对 trace。

验证：corner_edge_fit_test 完整 reference 39 组 exact；1676 帧及 25＋88 合成输出金样。

裁决／偏离：仅合并确证相同计算；策略不同但样本零命中的路线保留。有限数据不能证明未来不触发，直接删除会冒行为风险。详见路线统计。

### F18 · 完成 · 原问题：发布与时序分别实现四点 bbox

步骤：`D18/T04`。定位：[lib/core/observed_geometry_utils.hpp:11](../lib/core/observed_geometry_utils.hpp#L11)；[lib/pipeline/temporal_geometry.cpp:141](../lib/pipeline/temporal_geometry.cpp#L141)；[lib/corners/detection_publication.cpp:70](../lib/corners/detection_publication.cpp#L70)。

实际：publication 与 temporal 共用最终 float 四角连续 bbox，原入口保留；没有改用整数 boundingRect。

验证：publicationAngles、temporal_stabilizer_test、assignment_completion_test；全公开结果逐字相同。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F19 · 完成 · 原问题：成功状态与 optional 载荷由多处独立组装

步骤：`D14/D15/D17`。定位：[lib/corners/corner_types.hpp:483](../lib/corners/corner_types.hpp#L483)；[lib/corners/corner_types.hpp:485](../lib/corners/corner_types.hpp#L485)；[lib/corners/corner_types.hpp:487](../lib/corners/corner_types.hpp#L487)。

实际：角点、排序、语义结果由统一成功／失败函数构造状态、optional 和原因；步骤接口保留。

验证：corner_resolver_test、screen_order_test、semantic_resolver_test 状态／载荷一致性及完整输出对照。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F20 · 完成 · 原问题：原图缓存具体类型穿过角点步骤边界

步骤：`D10/D11`。定位：[lib/corners/corner_observation.hpp:17](../lib/corners/corner_observation.hpp#L17)；[lib/pipeline/decode_stage.cpp:57](../lib/pipeline/decode_stage.cpp#L57)；[lib/corners/corner_resolver.cpp:31](../lib/corners/corner_resolver.cpp#L31)。

实际：原图观察拥有帧内惰性缓存；resolver 只接收带 frame_id 的只读轮廓查询来源，不认识具体缓存类。

验证：corner_resolver_test 原索引生命周期、错帧、同 ID 改图，corner_edge_fit_test；全视频。

裁决／偏离：保留 OriginalContourIndex；不退回每假设全图重扫。

### F21 · 完成 · 原问题：角证据协议漏掉当前线模型字段

步骤：`D14/A07/V03`。定位：[tools/common/frame_record_reader.cpp:421](../tools/common/frame_record_reader.cpp#L421)；[lib/pipeline/diagnostics_recorder.cpp:254](../lib/pipeline/diagnostics_recorder.cpp#L254)；[tools/validation/path_a_verify.cpp:522](../tools/validation/path_a_verify.cpp#L522)。

实际：完整序列化线模型、参数及残差，角证据子版本为 2；path_a_verify 两处共用强类型读取，检查字段／身份／形状。

验证：diagnostics_record_test 四类线模型往返、独立复算、篡改／缺失／版本／来源负例。

裁决／偏离：外层 record_schema_version 仍 1；旧记录缺模型参数明确 UNREPLAYABLE，不猜零值或 Legacy。新增 fixture 构造问题修正后通过，未放松 reader。

### F22 · 完成 · 原问题：同次关联与校正重复创新求解

步骤：`K07/K08`。定位：[lib/tracking/kalman_cv.hpp:11](../lib/tracking/kalman_cv.hpp#L11)；[lib/tracking/kalman_cv.cpp:180](../lib/tracking/kalman_cv.cpp#L180)；[lib/tracking/target_tracker.cpp:304](../lib/tracking/target_tracker.cpp#L304)。

实际：同状态创新残差／S／R／评分显式传给校正；实例和版本阻止跨更新复用，独立 correct 仍校验。

验证：M01–M12／T01–T51，独立创新评分／Joseph 公式、错实例／过期拒绝；四组合完整字节对照。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F23 · 完成 · 原问题：事务复制连同不可变配置一起复制

步骤：`K02/K09`。定位：[lib/tracking/target_tracker.cpp:37](../lib/tracking/target_tracker.cpp#L37)；[lib/tracking/target_tracker.cpp:360](../lib/tracking/target_tracker.cpp#L360)。

实际：只读配置与可变 TrackerState 分开；只复制候选 State，输入接受后一次提交，方法显式接配置。

验证：tracking_contract_test 坏输入回滚、ID／重建／实例隔离及只读预测；四组合轨迹逐字一致。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F24 · 完成 · 原问题：同帧时间资格和同状态标准差反复计算

步骤：`K04/K10`。定位：[lib/tracking/target_tracker.cpp:213](../lib/tracking/target_tracker.cpp#L213)。

实际：同帧 time_bounded 只计算一次，create／更新消费本帧已有结果。

验证：T45/T46/T50/T51 及四组合 profile／全部轨迹逐字一致。

裁决／偏离：复核发现 snapshot 标准差在当前短路分支已各读一次；保留求值顺序，不额外新增无收益 UncertaintySummary 或提前测量。

### F25 · 不改 · 原问题：精度资格仍决定部分数值字段是否存在

步骤：`K10/K11`。定位：[lib/tracking/target_tracker.cpp:121](../lib/tracking/target_tracker.cpp#L121)。

实际：不实施估计／资格语义变更，保留现行状态、输出及资格规则。

验证：G4 四组合完整输出与原二进制逐字一致；公开 G4 头、tracking.yaml 无 diff。

裁决／偏离：按六条定法只记录未来方向：估计与资格分离供 G3/G5 衔接；本次不改这项语义。

### F26 · 完成 · 原问题：export 全历史 ID 集合不能再拒绝额外状态

步骤：`A07/A10`。定位：[lib/pipeline/diagnostics_recorder.cpp:703](../lib/pipeline/diagnostics_recorder.cpp#L703)。

实际：删除 export_ids_ 成员、历史 push/find；保留单调帧号、前一待补项和重复完成检查。

验证：diagnostics_record_test exports／completion／生命周期负例。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F27 · 完成 · 原问题：同一帧渲染证据重复核验

步骤：`A08`。定位：[app/debug_renderer.cpp:26](../app/debug_renderer.cpp#L26)；[app/offline_runner.cpp:354](../app/offline_runner.cpp#L354)。

实际：证据检查仅在适配时执行一次，结果同时交给事件记录和渲染；独立 render 入口仍自行核验。

验证：debug_renderer_test mismatch/off/immutable；视频采样回归及完整诊断探针对照。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F28 · 完成 · 原问题：公开 DetectorConfig 持有应用显示配置

步骤：`Q03/A03/A08`。定位：[lib/config/app_config.hpp:19](../lib/config/app_config.hpp#L19)；[lib/config/observability_config.hpp:13](../lib/config/observability_config.hpp#L13)；[lib/config/config.cpp:176](../lib/config/config.cpp#L176)。

实际：窗口归 AppConfig，held/candidates 归 RenderConfig；新 display 段只有一份值，runner 不再覆盖另一份显示配置。

验证：全仓消费者检索、非默认显示值 roundtrip、旧 output/debug 显示键逐项拒绝；全量回归。

裁决／偏离：正式迁移公开布局，与 F10/F31 同批；schema_version 保留 1，旧键明确拒绝，不做隐式双协议。

### F29 · 完成 · 原问题：预处理与映射注释描述的流程已过时

步骤：`D02`。定位：[lib/preprocess/preprocess.hpp:10](../lib/preprocess/preprocess.hpp#L10)。

实际：纠正 PreparedFrame 是 resize 后 BGR8、后续观察才二值化，以及像素中心双向映射的注释。

验证：preprocessMapping／debug_renderer_test mapping；图像、阈值与全部公开结果不变。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F30 · 完成 · 原问题：教学复述和过时 TODO 遮住实际配置契约

步骤：`Q01/Q03`。定位：[lib/core/config_error.hpp:6](../lib/core/config_error.hpp#L6)。

实际：删 ConfigError 待实现 TODO、runtime_error 教学伪代码、config 调试复述及三份 YAML 过时注释；同步删改动测试处死注释。

验证：config_error_test、roundtrip、temporal；三份 YAML 本批非注释 token 完全相同。

裁决／偏离：不改历史文档原文；F68 尾部旧骨架另计，避免重复声称删除量。

### F31 · 完成 · 原问题：Skeleton 已表示完整检测实现

步骤：`Q02/D01`。定位：[include/mark/detector_config.hpp:25](../include/mark/detector_config.hpp#L25)；[config/detector.yaml:27](../config/detector.yaml#L27)。

实际：枚举／YAML／消费者直接改名 SegmentedMark；Skeleton／skeleton 只作为显式历史或拒绝用例。

验证：旧值拒绝、完整配置 roundtrip、全仓代码与现行 Markdown 检索。

裁决／偏离：与 F10/F28 同批；唯一模式仍执行完整流水线，不新增模式或改变就绪语义。

### F32 · 完成 · 原问题：严格 CLI 解析在多个入口重复

步骤：`A01/V02`。定位：[lib/core/cli_options.hpp:11](../lib/core/cli_options.hpp#L11)；[app/tracking_main.cpp:18](../app/tracking_main.cpp#L18)；[app/main.cpp:101](../app/main.cpp#L101)。

实际：五入口共用 key/value/flag 形状检查，允许项、起始位置、值策略和原错误上下文由入口提供；两处共用严格十进制 uint64。

验证：77 组实际 CLI／配置与原源码退出/stdout/stderr 完全相同；独立 flags／值／start-index／MAX／溢出回归。

裁决／偏离：geometry/temporal 保留旧“值可呈选项形状”策略；没有因合并而改变合法路径名或错误顺序，也没有命令框架。

### F33 · 完成 · 原问题：G2/G4/Detector 重复节点读取原语但策略不同

步骤：`Q01/Q02`。定位：[lib/config/value_reader.hpp:9](../lib/config/value_reader.hpp#L9)；[lib/config/pose_config.cpp:18](../lib/config/pose_config.cpp#L18)；[lib/config/tracking_config.cpp:13](../lib/config/tracking_config.cpp#L13)。

实际：共享 required/text/integer/bool/number 原语；Detector 严格节点、G2 普通严格／optional 完整数值串、G4 完整数值串与空串未知明确区分。

验证：实际 Detector/G2/G4 数字／数字串／空／NaN／坏类型与旧实现逐字对照；各配置／位姿／跟踪测试。

裁决／偏离：只新增必要私有普通函数头，多消费者共用；模块 schema 与抛错文本仍各自拥有，未统一成宽松协议。

### F34 · 完成 · 原问题：整数读取和写回旁路散落在长流程中

步骤：`Q02/Q06`。定位：[lib/config/config.cpp:68](../lib/config/config.cpp#L68)；[lib/config/config.cpp:95](../lib/config/config.cpp#L95)。

实际：有符号 token 复核、uint64 精确读取和占位写回从字段装配中分出 codec；原 regex、节点交叉检查与写失败保护保留。

验证：hold 溢出负例、UINT64_MAX roundtrip／output_config；实际配置协议 77 组对照。

裁决／偏离：不退回 double/int，也不趁整理改变已有 token 规则或阈值。

### F35 · 完成 · 原问题：空配置测试仍登记为成功目标

步骤：`B04/V04`。定位：[CMakeLists.txt:62](../CMakeLists.txt#L62)。

实际：删除无断言的 config_contract_test.cpp 及目标登记；真正坏配置、往返、构造契约全部保留。

验证：默认 23/23、启用 G2/G4 全量 27/27；只与 F09 合计少两个报告授权目标。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F36 · 完成 · 原问题：Tag 构造与 G2 配置重复参数校验

步骤：`Q03/P03`。定位：[lib/pose/pose_geometry.cpp:14](../lib/pose/pose_geometry.cpp#L14)。

实际：两个入口共用完全相同的 Tag 数值谓词；各自错误文字、可选 margin、原生对象／family／白名单尺寸检查保留。

验证：tag_detector_test、pose_contract_test，G2 实际配置协议对照。

裁决／偏离：共享纯参数条件，不把 native 对象创建或 ID 范围假装为普通配置默认。

### F37 · 完成 · 原问题：未启用法向约束仍转换 Rodrigues，唯一解统计额外建表

步骤：`P08/P09`。定位：[lib/pose/pose_estimator.cpp:258](../lib/pose/pose_estimator.cpp#L258)。

实际：法向约束启用时才 Rodrigues；唯一性只存 eligible_count／sole_index，完整候选列表与歧义保留。

验证：pose_contract_test T01/T15、安装／先验／LM 回退相关回归。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F38 · 完成 · 原问题：prepare/process/take 隐藏一次调用所需状态

步骤：`A05/A06`。定位：[lib/pipeline/detector.cpp:191](../lib/pipeline/detector.cpp#L191)；[app/offline_runner.cpp:298](../app/offline_runner.cpp#L298)。

实际：内部单次桥管理 prepare/process/take，返回结果／记录／异常及外部簿记时间；失败记录先移出再由应用保存／重抛。

验证：access/statuses/instances 及新增桥调用后继续帧、非法帧恢复；process_total 仍 public_call。

裁决／偏离：旧公共 process 与错序守卫保留；未增加公共 observer 或改变 Detector 对象布局。

### F39 · 完成 · 原问题：机器原因通过文字约定再反向分类

步骤：`T04/A06/A12`。定位：[lib/pipeline/temporal_types.hpp:6](../lib/pipeline/temporal_types.hpp#L6)；[lib/pipeline/stabilize_stage.cpp:69](../lib/pipeline/stabilize_stage.cpp#L69)。

实际：生产步骤显式给机器原因枚举，stabilize_stage 不再解析显示字符串；旧 reason 文本仅作编码/detail，InputChanged 控制也读类型。

验证：ZERO_DT／对应歧义／平滑偏离／关联歧义类型回归、console unknown；1676 完整公开结果与 diagnostics 逐字一致。

裁决／偏离：保留旧 detail 字段作为兼容载荷，不删除 unknown fallback，不改变 JSON／终端字符串。

### F40 · 完成 · 原问题：三个完整文件 fingerprint 实现重复

步骤：`A02/V03`。定位：[lib/core/file_fingerprint.hpp:9](../lib/core/file_fingerprint.hpp#L9)。

实际：三处完整文件 FNV 共用流式字节函数，调用者给原 IO 错误上下文；结果指纹和 SHA256 协议独立。

验证：空文件、256000 字节二进制、打开／读取失败；真实 marker 运行输入指纹及独立验证通过。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F41 · 完成 · 原问题：temporal_audit 的 overwrite flag 没有执行作用

步骤：`V02`。定位：[tools/audit/temporal_audit.cpp:205](../tools/audit/temporal_audit.cpp#L205)。

实际：删未使用的 overwrite flag 和解析变量；旧选项明确 unknown，已有目录继续拒绝覆盖。

验证：--overwrite 退出 1 且 unknown；77 组 CLI 对照及目录生命周期测试。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F42 · 完成 · 原问题：同一翻译单元重复 include

步骤：`P12/V04`。定位：[tools/validation/pose_sensitivity.cpp:5](../tools/validation/pose_sensitivity.cpp#L5)。

实际：删除 pose_sensitivity 与 screen_order_test 的重复 include，仅静态去重。

验证：完整构建及 screen_order／G2 回归。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F43 · 完成 · 原问题：诊断工具复制算法前半段的执行顺序

步骤：`V02/D02…D09`。定位：[lib/pipeline/decode_stage.cpp:180](../lib/pipeline/decode_stage.cpp#L180)；[tools/block3_fixture/diagnostics/video_diagnose.cpp:136](../tools/block3_fixture/diagnostics/video_diagnose.cpp#L136)。

实际：正式链与三个诊断／验证入口共用几何步骤编排，显式选 Validated 或 Completed 产物；归因探针仍可单独调用步骤。

验证：三个工具构建；全视频 7 选帧 video_diagnose 新旧完整 JSON 逐字相同，主链金样一致。

裁决／偏离：工具不额外添加生产 parent trace；pipeline_trace 显式保留原生产记录范围。

### F44 · 完成 · 原问题：parent_diagnose 把 component_id 当数组下标

步骤：`V02/D03/D04`。定位：[tools/block3_fixture/diagnostics/parent_diagnose.cpp:25](../tools/block3_fixture/diagnostics/parent_diagnose.cpp#L25)。

实际：parent probe 按组件 ID 和观察来源位置关联，不再拿 ID 当数组下标。

验证：identity.log 的稀疏／重排／重复／缺失 ID 与合法输出 baseline cmp。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F45 · 完成 · 原问题：相同 gate 归因配置复制两遍

步骤：`V02`。定位：[tools/block3_fixture/diagnostics/video_diagnose.cpp:85](../tools/block3_fixture/diagnostics/video_diagnose.cpp#L85)。

实际：两套归因 gate 从同一普通函数取得配置副本；标签、顺序、实验值保留，生产配置不回写。

验证：两入口全部单帧诊断 JSON 新旧逐字一致；工具构建通过。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F46 · 完成 · 原问题：旧 candidate-config 子命令继续导出历史 recipe

步骤：`V02/Q06`。定位：[tools/block3_fixture/validation/verify.cpp:289](../tools/block3_fixture/validation/verify.cpp#L289)。

实际：删除静态历史 recipe 导出实现及现行说明；--candidate-config 明确拒绝，不填新预算。

验证：移除选项退出 1、unknown；measure／verify 目标构建及生产配置保护复查。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F47 · 完成 · 原问题：Python 证据断言在优化模式会被整体移除

步骤：`V03/V04`。定位：[tools/validation/recovery112_check.py:17](../tools/validation/recovery112_check.py#L17)；[tools/block3_fixture/reporting/summarize_video_corners.py:13](../tools/block3_fixture/reporting/summarize_video_corners.py#L13)。

实际：四个 Python 证据工具的 28 处必要 assert 改为显式 require，条件不删。

验证：普通／-O 坏输入均失败；9 个合法／缺测统计场景两模式通过；显式 replay 两模式输出完整。

裁决／偏离：第四个 summarize_video_corners 同属必要证据条件，一并修复；不是新增验证门槛。

### F48 · 完成 · 原问题：无有效测量时汇总崩溃或把缺失记零

步骤：`V05`。定位：[tools/block3_fixture/reporting/summarize_video_corners.py:18](../tools/block3_fixture/reporting/summarize_video_corners.py#L18)。

实际：CSV 固定列；零样本分布和单样本方差为 null/N/A，失败行保留；物理角缺分布不给候选预算。

验证：9 场景（全失败／单样本／局部有效／缺物理角等）；原合法输出 Markdown/JSON/CSV baseline cmp。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F49 · 完成 · 原问题：112 验证驱动绑定本仓目录和历史证据

步骤：`V06/B06`。定位：[tools/validation/recovery112_check.py:22](../tools/validation/recovery112_check.py#L22)；[tools/validation/recovery112_check.py:29](../tools/validation/recovery112_check.py#L29)。

实际：source_root/input_manifest/output_root/build_logs 显式输入；按冻结目标集合及全过摘要检查，新增目标不误判固定总数。

验证：两个临时根、缺输入不建成功产物、动态目标集合；普通/-O 完整两帧 replay 的 manifest／source 快照。

裁决／偏离：历史原件不改。manifest 由调用者提供，不新增仓库第三份配置；坏数据、完整 reference 和原性能／精度条件全保留。

### F50 · 部分完成 · 原问题：夹具工具递归导入完整 detector 工程

步骤：`B01/B02`。定位：[tools/block3_fixture/CMakeLists.txt:17](../tools/block3_fixture/CMakeLists.txt#L17)。

实际：父工程已提供 detector／synthetic 目标时直接链接，不重复导入；工具继续链接既有私有接口目标。

验证：根默认／全量构建测试及独立夹具五目标构建。

裁决／偏离：部分完成：独立工具仍需旧根 add_subdirectory 装配；彻底消除向上导入需要先有模块目标，按用户裁决留搬家。

### F51 · 完成 · 原问题：全目录源码 hash 与实际目标输入不一致

步骤：`B05`。定位：[CMakeLists.txt:218](../CMakeLists.txt#L218)；[app/main.cpp:115](../app/main.cpp#L115)；[tools/synth_ref/CMakeLists.txt:11](../tools/synth_ref/CMakeLists.txt#L11)。

实际：按每目标源码、工程内双引号 include、实际链接图、CMake／模板及构建参数生成 manifest；实际 CLI 显式传自身身份，合成 core/CLI/test 分开。

验证：未链接 G2/G4 隔离、真实依赖传播、模块 CMake、SG test/CLI/core 隔离；运行 manifest code_sha256 等于自身 sources.manifest 的 SHA256。

裁决／偏离：不拆 CMake 文件。根 CMake 作为实际输入整文件登记，粒度留布局搬迁；SG 环境 code_hash 记核心库身份，CLI/test 另存目标 manifest。

### F52 · 部分完成 · 原问题：工具依赖和测试登记没有独立构建开关边界

步骤：`B01/B04`。定位：[CMakeLists.txt:107](../CMakeLists.txt#L107)。

实际：OpenSSL 查找紧贴实际审计目标；G2 包查找仍在既有开关内，JSON／合成依赖仍在实际工具 CMake。

验证：默认 23/23、G2/G4 27/27、合成 7/7、夹具五目标构建。

裁决／偏离：部分完成：默认仍构建审计工具；模块 BUILD_TESTING/BUILD_TOOLS 与模块级查找隔离按裁决留搬家，不新建模块 CMake 或改默认行为。

### F53 · 完成 · 原问题：步骤头只是统一 core.hpp 的转发壳

步骤：`S02…S08`。定位：[tools/synth_ref/include/synthetic_gen/core.hpp:2](../tools/synth_ref/include/synthetic_gen/core.hpp#L2)；[tools/synth_ref/include/synthetic_gen/model.hpp:48](../tools/synth_ref/include/synthetic_gen/model.hpp#L48)；[tools/synth_ref/include/synthetic_gen/grid.hpp:6](../tools/synth_ref/include/synthetic_gen/grid.hpp#L6)。

实际：八个步骤头各自拥有声明／必要类型；core.hpp 仅保留兼容聚合，消费者显式包含所用步骤。

验证：9 个既有步骤／聚合头逐个独立编译，合成 7/7，夹具生成／测量／verify 构建。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F54 · 完成 · 原问题：大 Context 和混合文件使步骤互知实现

步骤：`S01/S03/S06/S07`。定位：[tools/synth_ref/src/records.cpp:155](../tools/synth_ref/src/records.cpp#L155)；[tools/synth_ref/src/transforms.cpp:35](../tools/synth_ref/src/transforms.cpp#L35)；[tools/synth_ref/src/cases.cpp:133](../tools/synth_ref/src/cases.cpp#L133)。

实际：私有仿射、场景、扰动、真值编码和 grid/case 核验只接所需只读切片／值；创建运行目录显式接路径。

验证：model/transforms/grid/raster/perturb/cases/workflow 与篡改测试，重复真值／PNG 身份保持。

裁决／偏离：公开 Context／函数签名保留；在原文件中拆普通函数，不进行布局片段搬家或新建来源/事务框架。

### F55 · 完成 · 原问题：verify 手写另一文件函数声明

步骤：`S06/S08`。定位：[tools/synth_ref/include/synthetic_gen/records.hpp:10](../tools/synth_ref/include/synthetic_gen/records.hpp#L10)。

实际：四个 record_* 编码声明归 records.hpp，records 与 verify 共用，verify 私自前向声明删除。

验证：workflow 列头／索引／PNG 篡改及独立头编译。

裁决／偏离：只共享格式编码，独立几何／真值判断不调用生成器结论。

### F56 · 完成 · 原问题：桥接连通复核计算未使用中心

步骤：`S05`。定位：[tools/synth_ref/src/cases.cpp:133](../tools/synth_ref/src/cases.cpp#L133)。

实际：删除未使用的中心坐标局部变量，真实场景变换与样例定义保留。

验证：合成 7/7、重复真值／PNG hash 一致。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F57 · 完成 · 原问题：合成测试借全局 evidence_directory 和 cwd 传数据

步骤：`V06/S01`。定位：[tools/synth_ref/tests/tests.cpp:13](../tools/synth_ref/tests/tests.cpp#L13)；[tools/synth_ref/src/model.cpp:354](../tools/synth_ref/src/model.cpp#L354)；[tools/synth_ref/tests/tests.cpp:14](../tools/synth_ref/tests/tests.cpp#L14)。

实际：证据根归 main 局部并传 workflow；loader(root) 显式定位来源，临时 cwd 切换局部 RAII 恢复。

验证：两个独立临时工作根、从 /tmp 运行 workflow、故意中断后的 cwd 恢复与显式根身份。

裁决／偏离：旧零参 CLI 入口仍适配工具根 cwd，公开 Context 不改；临时目录切换限兼容事务边界，不作为步骤接口。

### F58 · 完成 · 原问题：标定范围选项读验后未参与选帧

步骤：`C01/C03`。定位：[tools/calibration/calibration_config.hpp:33](../tools/calibration/calibration_config.hpp#L33)。

实际：删除无实际选帧消费者的 start_frame/end_frame、CLI/YAML 及现行说明；旧范围键／参数明确拒绝。

验证：frame_list 合法与坏类型／负数／越界、旧范围键／CLI 负例；真实 52 选帧标定。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F59 · 完成 · 原问题：视频尺寸被回写静态配置传递

步骤：`C02/C05/C06`。定位：[tools/calibration/calibration_result.hpp:13](../tools/calibration/calibration_result.hpp#L13)；[tools/calibration/calibration_video.hpp:10](../tools/calibration/calibration_video.hpp#L10)。

实际：尺寸／来源由 VideoInfo 显式交给 solver，保存在结果，writer 不从静态配置回读运行尺寸。

验证：320×240／640×480 固定点对求解与保存往返；真实标定新旧 YAML 逐字一致。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F60 · 完成 · 原问题：圆点阵失败样本丢掉来源身份

步骤：`C03/C04/C05`。定位：[tools/calibration/calibration_detection.hpp:5](../tools/calibration/calibration_detection.hpp#L5)；[tools/calibration/calibration_result.cpp:44](../tools/calibration/calibration_result.cpp#L44)。

实际：SampledFrame 保留 frame_id，所有成功／失败帧都有 BoardObservation；solver 显式筛成功点对并保留 frame_ids 映射。

验证：成功／失败交错样本与来源映射；真实 52 帧中 49 成功，新旧标定结果字节相同。

裁决／偏离：失败只作信息记录；输出 calibration.yaml 原字段不变，不把坏样本补成观测。

### F61 · 完成 · 原问题：标定解析和数值合法性口径不完整

步骤：`C01/Q02`。定位：[tools/calibration/calibration_config.cpp:20](../tools/calibration/calibration_config.cpp#L20)。

实际：严格检查 YAML／CLI 未知、重复、缺值、类型和完整数值；原 token 防 FileStorage 32 位窄化，间距必须有限正值。

验证：初批标定 7 组检查及 config_error_test 中 YAML/CLI/NaN/Inf/溢出负例；最终完整构建与测试。

裁决／偏离：F58 移除范围键后按新协议拒绝；不把旧范围成功命令当现行行为。

### F62 · 完成 · 原问题：选帧工具读帧失败仍返回成功

步骤：`C03`。定位：[tools/calibration/scripts/frame_picker.py:26](../tools/calibration/scripts/frame_picker.py#L26)。

实际：视频 seek/read 失败明确非零；选帧列表保留，用户结束与输入结束区分。

验证：帧选择 mock 场景及标定输入检查。

裁决／偏离：不改变原导航／自动退出行为，不新增抽帧功能。

### F63 · 完成 · 原问题：活动导航仍描述没有 G2/G4 的旧系统

步骤：`P01/K01/V02`。定位：[docs/INDEX.md:3](../docs/INDEX.md#L3)；[docs/dataflow-interfaces.md:88](../docs/dataflow-interfaces.md#L88)；[tools/INDEX.md:79](../tools/INDEX.md#L79)。

实际：README、文档／工具索引与受牵连现行接口同步；历史审核／验收仅加状态与当前入口，不改原结论。

验证：全仓 Markdown 标识符复查、路径检查；只读报告／底稿及 history/ref/evidence 无 diff。

裁决／偏离：新增交付文档各自最后单独提交；旧验收数字按原时点解释，不重写成当前结果。

### F64 · 完成 · 原问题：timing.cpp 混装重置与所有记录枚举命名

步骤：`A06/A12`。定位：[lib/core/diagnostics_types.hpp:166](../lib/core/diagnostics_types.hpp#L166)；[lib/core/timing.cpp:7](../lib/core/timing.cpp#L7)。

实际：ResetSnapshot 与记录枚举编码归现有 diagnostics_types.hpp；timing.cpp 只保留计时功能。

验证：timing_test、instances、diagnostics_record_test；完整记录／公开结果字符串对照。

裁决／偏离：短协议函数 inline，不为本次布局新建 frame_events.cpp，声明／字符串与饱和语义不变。

### F65 · 不改 · 原问题：公开头路径迁移需要调用方兼容决策

步骤：`B02/P12`。定位：[include/mark/detector.hpp:40](../include/mark/detector.hpp#L40)。

实际：公开 mark/*.hpp 包含路径与 namespace 保持，不进行迁移兼容层调整。

验证：完整默认／G2/G4／工具构建；公开包含路径未移动。

裁决／偏离：未做：用户明确仍未定，不擅自做公开 include 迁移。

### F66 · 完成 · 原问题：假设组装接收未使用观察和模型

步骤：`D06`。定位：[lib/geometry/geometry_matcher.hpp:12](../lib/geometry/geometry_matcher.hpp#L12)。

实际：内部组装只收 assignments 和 affine，未读的 observations/model 参数删除；组装步骤保留。

验证：geometry_matcher_test、geometry_anchor_evidence_test、path_a_competition_test 与全帧对照。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F67 · 完成 · 原问题：测试驱动重复，fixture 又互借通用入口

步骤：`V04/V06`。定位：[test_support/case_runner.hpp:8](../test_support/case_runner.hpp#L8)；[test_support/assertions.hpp:6](../test_support/assertions.hpp#L6)。

实际：通用 check／case runner 归两个极小测试头，fixture 不再互借通用入口，独立 oracle／reference 保留。

验证：31 个用例名称／顺序不变；故意失败返回 1 且继续后例；默认／全量测试通过。

裁决／偏离：报告明确要求的多消费者能力头不视作模块搬家；不移动文件，不引入测试框架，也不改断言期望。

### F68 · 完成 · 原问题：config.cpp 留有整段失效注释实现

步骤：`Q01/Q03/Q06`。定位：[lib/config/config.cpp:95](../lib/config/config.cpp#L95)。

实际：删除 config.cpp 尾部整段注释掉的旧实现；活跃读／验／写流程保留。

验证：config_error_test、roundtrip 及全部配置协议回归。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F69 · 完成 · 原问题：旧 temporal_record 薄包装已无消费者

步骤：`V03/A07`。定位：[tools/INDEX.md:66](../tools/INDEX.md#L66)。

实际：删除零消费者 tools/common/temporal_record.hpp 及 CMake hash／工具索引旧行；实际 recorder 接口保留。

验证：全仓现行源零引用、完整构建与 temporal/record 回归。

裁决／偏离：按报告执行；公开行为、阈值和空预算不变。

### F70 · 完成 · 原问题：同一 double 四角被连续排序复核三次

步骤：`D14/D15/D17`。定位：[lib/corners/corner_resolver.hpp:18](../lib/corners/corner_resolver.hpp#L18)；[lib/pipeline/decode_stage.cpp:61](../lib/pipeline/decode_stage.cpp#L61)。

实际：resolver 交付同一 double 四角的排序结果；decode 几何验证与语义检查显式复用，主链不连续排序三次。

验证：screen_order_test、semantic_resolver_test、publicationAngles、完整 1676 结果及 39 reference。

裁决／偏离：旧独立入口仍自行检查；float 舍入后是新输入，发布／时序仍独立排序，不能复用 double 缓存。

### F71 · 完成 · 原问题：测试资源定位隐含固定目录深度

步骤：`Q04/V01/V06`。定位：[CMakeLists.txt:52](../CMakeLists.txt#L52)；[tests/config_error_test.cpp:1](../tests/config_error_test.cpp#L1)。

实际：测试目标显式提供资源根，配置／模型／数据不再猜 __FILE__ 父目录深度；配置负例的独占 test_root 显式传到每个测试函数。

验证：从 /tmp 运行资源测试，最终默认 23/23 与全量 27/27 并行全过；隔离配置负例再次并行通过。

裁决／偏离：最终并行验收发现两构建固定 /tmp 文件名互相覆盖；按本项显式 TestPaths 方法修正，保留首轮失败日志，未改变算法或期望。

## 受牵连的文档同步

以下 15 份已有文档随对应代码批次提交；新增交付仅本报告与流程导读。表中“只加历史状态”的文件没有重写历史数值或验收结论。

| 文档定位 | 实际同步 |
|---|---|
| [README.md:5](../README.md#L5) | 快速入口链接两份交付，说明现行模式与 display 协议。 |
| [docs/INDEX.md:3](../docs/INDEX.md#L3) | 现行阅读入口与历史验收数字的适用状态。 |
| [docs/architecture/INDEX.md:3](../docs/architecture/INDEX.md#L3) | 把旧整理记录与现行流程入口区分。 |
| [docs/architecture/include-dependency-analysis.md:3](../docs/architecture/include-dependency-analysis.md#L3) | 只加历史状态／现行链接，不改原依赖快照与结论。 |
| [docs/block1.md:3](../docs/block1.md#L3) | 只加历史状态，保留当时配置、名字与验收原文。 |
| [docs/block2.md:3](../docs/block2.md#L3) | 只加历史状态，原测试数字与验收内容不改写。 |
| [docs/block4_budget.md:3](../docs/block4_budget.md#L3) | 只加历史状态／现行链接，原预算结论保留。 |
| [docs/dataflow-interfaces.md:88](../docs/dataflow-interfaces.md#L88) | 同步步骤产物、模块校验、配置迁移、typed 原因、桥与运行结论。 |
| [docs/diagnostics_schema.md:7](../docs/diagnostics_schema.md#L7) | 同步完整角证据子版本、来源／scope／运行结论和现行记录约束。 |
| [docs/fix2_sweep_report.md:3](../docs/fix2_sweep_report.md#L3) | 只加历史状态／现行入口，旧报告和验收结论保留。 |
| [docs/g4.md:97](../docs/g4.md#L97) | 补充内部创新／事务整理的边界，公开输出、资格和 OPEN 状态不变。 |
| [tools/INDEX.md:17](../tools/INDEX.md#L17) | 删除失效包装入口，说明旧选项拒绝、显式输入与当前共享步骤。 |
| [tools/calibration/README.md:50](../tools/calibration/README.md#L50) | frame_list、显式元信息／样本身份及旧范围输入拒绝。 |
| [tools/calibration/scripts/frame_picker_README.md:67](../tools/calibration/scripts/frame_picker_README.md#L67) | 明确读失败与主动停止的区别及退出结果。 |
| [tools/synth_ref/README.md:105](../tools/synth_ref/README.md#L105) | 显式测试根与核心／CLI／test 来源身份边界。 |

最终检索全部现行 Markdown：旧名称／选项仅在明确历史状态、拒绝说明或只读历史文件中出现；没有把历史旧配方改成现行建议。lean-report.md、move-layout-draft.md、各 evidence/ref/history 原文与验收结论不改。

## 六条定法落地

| 定法 | 实际落地 |
|---|---|
| 13 条回退路线 | 完整清单及触发顺序保留；显式表；两处同输入计算复用且 trace 保持；统计只在临时工具侧。 |
| F10 三旋钮 | 公开字段及读验写删除，旧 YAML 键拒绝，实际截断／坏角拒绝保留。 |
| F28 显示字段 | AppConfig.show_window／RenderConfig 唯一持有，新 display 协议，旧 output/debug 显示键拒绝。 |
| F31 正式改名 | SegmentedMark 一步到位，代码／YAML／当前文档同步，历史旧值明确拒绝。 |
| F08 隔离快照 | 按值返回 G2Config，K/dist 深拷贝；调用方修改不影响实例。 |
| F25 只定方向 | 不实施估计／资格语义改动。F22–F24 仅内部整理且四组合完整字节对照通过。 |

F65 公开 include 路径未动。F50 的独立向上装配、F52 的模块 BUILD_TESTING/BUILD_TOOLS 边界留搬家；这是用户明确允许的未做项。

## 13 条回退路线与命中数据

位置从 1 开始；原 trace stage 从 0 开始。每一路的共同触发条件是前面已启用路线均无 evidence；首次成功立即返回。`原端点/全端点` 指支持弧端点集合，`原位置/后验位置` 指对应位置校验方式；拟合仍来自当前真实像素。OriginalOnly 仅 1，LinearRecovery 为 1/6/7，PeriodicRecovery 为 1/6/7/8–13，FastRecovery 为全部 13 路。

| 位置 | 线模型 | 端点／位置 | 单元测试尝试／命中 | 实拍尝试／命中 | 收尾形态 |
|---|---|---|---:|---:|---|
| 1 | Legacy | 原／原 | 49/40 | 4360/4244 | 保留独立策略 |
| 2 | PhaseMedian | 原／原 | 6/4 | 116/113 | 保留独立策略 |
| 3 | ZeroSlope | 原／原 | 2/0 | 3/2 | 保留独立策略 |
| 4 | PhaseMedian | 原／后验 | 2/0 | 1/1 | 保留独立策略 |
| 5 | ZeroSlope | 原／后验 | 2/0 | 0/0 | 保留独立策略 |
| 6 | Legacy | 原／后验 | 5/0 | 0/0 | 保留独立策略 |
| 7 | Legacy | 全／原 | 5/3 | 0/0 | 保留独立策略 |
| 8 | PhaseMedian | 原／原 | 2/0 | 0/0 | Fast 复用 2，重放 trace |
| 9 | PhaseMedian | 原／后验 | 2/0 | 0/0 | Fast 复用 4，重放 trace |
| 10 | PhaseMedian | 全／原 | 2/0 | 0/0 | 保留独立策略 |
| 11 | L1 | 原／原 | 2/0 | 0/0 | 保留独立策略 |
| 12 | L1 | 原／后验 | 2/0 | 0/0 | 保留独立策略 |
| 13 | L1 | 全／原 | 2/0 | 0/0 | 保留独立策略 |

统计来自瘦身前的临时插桩 fitter，链接到测试／工具；未进入 shipped 代码。单元统计含四种策略入口；实拍为完整 1676 帧、一次次角拟合尝试的统计，不是帧成功率分母。路线 8/9 是确证相同参数重复，可合并计算；其他零命中路线不是“已证无触发”，故不删除。最终 39 组完整弧／配对／证据 reference exact，25 条标定／88 条测试 JSON 与冻结结果逐字相同。

## 关键裁决与过程中的失败

- 用户追加要求“裁决随批次记录”，交付又要求两份新文档最后独立提交：先保留 `/tmp/campus-lean-execution-ledger.json`、批次提交正文与日志，收尾在本报告汇总；没有提前修改只读报告。
- F03 只保留旧探针适配载体，主链已显式分离；F13 不增加无关坏输入的提前拒绝；F17 不以有限数据零命中删除独立策略；F24 不为形式新增当前不需要的重复缓存。
- F51 当前源码使用工程内双引号 include，按其实际依赖和链接图登记；生成头登记模板，避免自引用 hash。根 CMake 同文件粒度仍较粗，未借身份整理进行布局拆分。
- F67 的两个极小测试头、F32/F33/F40 的三个私有函数头是报告动作所需的真实多消费者代码共用；“只新增两份文档”按追加文档交付理解，没有额外新增说明文档。
- 首轮最终全量／默认套件并行时，配置负例共用固定 `/tmp` 文件，导致全量 config_error_test 失败；按 F71 显式独占 test_root 修复后两套并行全过。失败日志保留，没有删用例或改期望来变绿。
- 标定新正例最初圆点太小，现有检测器未检出；只修正测试图像，不改检测器参数。F21 新 fixture 构造缺字段的问题也仅修 fixture；所有生产 guard 保留。

## 验证与日志附录

日志、探针源码、目标 manifest、完整命令与大产物 SHA256 已另存于 Git 忽略目录 [`build/lean-validation-20261102`](../build/lean-validation-20261102)。实际路径：`/home/tushenghao/projects/campus-marker-opencv-assignment/src/tushenghao/build/lean-validation-20261102`。历史 evidence/ref/history 原文及两份只读分析文件无 diff。大逐帧产物仍保留在 `/tmp` 并登记 `large-artifact-hashes.json`，没有制作或提交 g4-delivery.zip。

### 完整构建与全量测试

以下实际在仓库根执行；默认不启用 G2/G4 时为 23 项，开启后二者各增 2 项，共 27 项。两个删除项仅为 F09/F35。

```bash
cmake -S src/tushenghao -B /tmp/campus-lean-default -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/campus-lean-default -j4
ctest --test-dir /tmp/campus-lean-default --output-on-failure
cmake -S src/tushenghao -B /tmp/campus-lean-build -DCMAKE_BUILD_TYPE=Release -DMARK_BUILD_G2=ON -DMARK_BUILD_G4=ON
cmake --build /tmp/campus-lean-build -j4
ctest --test-dir /tmp/campus-lean-build --output-on-failure
cmake -S src/tushenghao/tools/synth_ref -B /tmp/campus-lean-synth -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/campus-lean-synth -j4
ctest --test-dir /tmp/campus-lean-synth --output-on-failure
cmake -S src/tushenghao/tools/block3_fixture -B /tmp/campus-lean-fixture -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/campus-lean-fixture --target block3_fixture block3_measure block3_verify block3_parent_diagnose block3_video_diagnose -j4
```

结果：默认 **23/23**、全量 **27/27**、合成 **7/7**，夹具五目标均通过。日志分别为 `extra/campus-lean-default-clean-build.log`、`extra/campus-lean-default-verified-ctest.log`、`full/final-clean-build.log`、`full/final-verified-ctest.log`、`synth/final-all-*.log`、`extra/campus-lean-fixture-final-verified-build.log`。最后的配置负例隔离并行复测见 `full/final-isolated-config-test.log` 和 `extra/campus-lean-default-isolated-config-test.log`。

### 其他实际对照

| 验证 | 结果／日志（相对上述归档目录） |
|---|---|
| 77 组实际 CLI／配置 | 退出码/stdout/stderr 完全相同；`extra/lean-cli-config-compare.log`。 |
| parser 与 uint64 codec | flag/value/起始位置/错误类型、零/MAX/溢出通过；`extra/lean-cli-codec-probe.log`。 |
| FNV 文件身份 | 空、256000 字节二进制、打开/读失败，独立格式相同；`extra/lean-fnv-check.log`。 |
| 每目标 provenance | 未链接模块隔离、实际依赖传播、SG 库/CLI/test 分离；`extra/lean-provenance-check.log`、`extra/provenance-commands.log`。 |
| 实际运行身份 | marker manifest 的 code_sha256 等于自身 sources.manifest SHA256；`extra/lean-runtime-provenance-check.log`。 |
| 完整 reference | 39 组全部弧/配对/证据 exact；`routes/dedup-reference.log`。 |
| raw 输出／合成金样 | 1676 帧有序 raw 一致；25＋88 行逐字相同；`routes/dedup-*`。 |
| 完整公开结果 | 从 7a7ef66 重建，1676 帧 raw/tracks/display/diagnostics JSON 逐字相同；`public-original/public-result-compare.log`，SHA256 `48b9d730fbcd04e1cd419362f1f134bf0c3193c2165280b8567bc6e969137dc4`。 |
| 真实相机标定 | 52 选帧中 49 成功，新旧结果 YAML 逐字相同；`extra/lean-calibration-flow-actual.log`、`probes/run.log`、`probes/result.yaml`。 |
| Python 必要检查 | 普通/-O 合法及篡改/缺测场景通过；`priority-tools/mandatory-normal-verification.log`、`priority-tools/optimized-verification.log`、`extra/lean-python-flow-check.log`。 |
| ID 关联／gate 探针 | 稀疏/重排/重复/缺失 ID 与新旧 JSON 对照；`priority-tools/identity.log` 和 `extra/lean-video-*`。 |
| 合成步骤头 | 9 个既有头独立编译；`synth/steps-headers.log`；workflow 异常恢复／不同 cwd 通过。 |

完整探针命令／源码保留在 `probes/` 与本机 `/tmp/lean-*.py/.cpp`；它们不是 shipped 代码。配置比较的完整 77 项列表另在 `/tmp/lean-protocol-compare-yg91oavc/comparison.json`。

### G4 四组合最终标定、验证、回放

数据只由现有 tracking_lab 的固定配置生成，原生产 tracking.yaml 的 profile／时间预算空值保持。四个输入配置仅切换坐标模式与时间策略；保存于 `g4-inputs/`。原数据生成命令为：

```bash
/tmp/campus-lean-build/tracking_lab generate --config src/tushenghao/config/tracking_lab.yaml --out /tmp/campus-lean-g4-data
```

以下是本次最终执行的完整命令（每个输出目录当时均不存在）；每步均退出 0。

#### camera_relative-nominal_research

```bash
/tmp/campus-lean-build/tracking_lab calibrate --config /tmp/campus-lean-g4-reference/camera_relative-nominal_research.yaml --input /tmp/campus-lean-g4-data --out /tmp/campus-lean-final-g4/camera_relative-nominal_research/calibration
/tmp/campus-lean-build/tracking_lab verify --config /tmp/campus-lean-g4-reference/camera_relative-nominal_research.yaml --input /tmp/campus-lean-g4-data --profile /tmp/campus-lean-final-g4/camera_relative-nominal_research/calibration/profile.yaml --out /tmp/campus-lean-final-g4/camera_relative-nominal_research/verify
/tmp/campus-lean-build/tracking_app --config /tmp/campus-lean-final-g4/camera_relative-nominal_research/calibration/tracking_research.yaml --input /tmp/campus-lean-g4-data/holdout/holdout-seed-2026104901/frames.yaml --out /tmp/campus-lean-final-g4/camera_relative-nominal_research/replay
```

#### camera_relative-bounded_only

```bash
/tmp/campus-lean-build/tracking_lab calibrate --config /tmp/campus-lean-g4-reference/camera_relative-bounded_only.yaml --input /tmp/campus-lean-g4-data --out /tmp/campus-lean-final-g4/camera_relative-bounded_only/calibration
/tmp/campus-lean-build/tracking_lab verify --config /tmp/campus-lean-g4-reference/camera_relative-bounded_only.yaml --input /tmp/campus-lean-g4-data --profile /tmp/campus-lean-final-g4/camera_relative-bounded_only/calibration/profile.yaml --out /tmp/campus-lean-final-g4/camera_relative-bounded_only/verify
/tmp/campus-lean-build/tracking_app --config /tmp/campus-lean-final-g4/camera_relative-bounded_only/calibration/tracking_research.yaml --input /tmp/campus-lean-g4-data/holdout/holdout-seed-2026104901/frames.yaml --out /tmp/campus-lean-final-g4/camera_relative-bounded_only/replay
```

#### local_world-nominal_research

```bash
/tmp/campus-lean-build/tracking_lab calibrate --config /tmp/campus-lean-g4-reference/local_world-nominal_research.yaml --input /tmp/campus-lean-g4-data --out /tmp/campus-lean-final-g4/local_world-nominal_research/calibration
/tmp/campus-lean-build/tracking_lab verify --config /tmp/campus-lean-g4-reference/local_world-nominal_research.yaml --input /tmp/campus-lean-g4-data --profile /tmp/campus-lean-final-g4/local_world-nominal_research/calibration/profile.yaml --out /tmp/campus-lean-final-g4/local_world-nominal_research/verify
/tmp/campus-lean-build/tracking_app --config /tmp/campus-lean-final-g4/local_world-nominal_research/calibration/tracking_research.yaml --input /tmp/campus-lean-g4-data/holdout/holdout-seed-2026104901/frames.yaml --out /tmp/campus-lean-final-g4/local_world-nominal_research/replay
```

#### local_world-bounded_only

```bash
/tmp/campus-lean-build/tracking_lab calibrate --config /tmp/campus-lean-g4-reference/local_world-bounded_only.yaml --input /tmp/campus-lean-g4-data --out /tmp/campus-lean-final-g4/local_world-bounded_only/calibration
/tmp/campus-lean-build/tracking_lab verify --config /tmp/campus-lean-g4-reference/local_world-bounded_only.yaml --input /tmp/campus-lean-g4-data --profile /tmp/campus-lean-final-g4/local_world-bounded_only/calibration/profile.yaml --out /tmp/campus-lean-final-g4/local_world-bounded_only/verify
/tmp/campus-lean-build/tracking_app --config /tmp/campus-lean-final-g4/local_world-bounded_only/calibration/tracking_research.yaml --input /tmp/campus-lean-g4-data/holdout/holdout-seed-2026104901/frames.yaml --out /tmp/campus-lean-final-g4/local_world-bounded_only/replay
```

四组合均与旧二进制对照：profile.yaml、holdout_errors.csv、三条 holdout 全轨迹及公开 replay tracks.yaml **逐字相同**。墙钟耗时不参与数值/语义相等比较，也不据此宣称实时性能已验收。最终对照见 `extra/lean-g4-final-check.log`，逐条命令及退出码见 `g4/<组合>/commands.jsonl`。

这组验证覆盖合成域现行行为，没有关闭 G-01/G-02/G-03、没有填硬件预算，也没有改变 F25/F65 的未做状态。
