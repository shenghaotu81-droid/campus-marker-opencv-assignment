# 按程序执行顺序阅读布局重构后的代码

从 main 进入，按下面的连续编号读到输出；编号是阅读／调用展开顺序，不是新加的运行阶段或类。旧 lean-report 的步骤码只在对应步骤首次出现时括注一次，供核对，不再作为主编号。MARK 主链先完整展开；位姿和三维跟踪是各有 main 的分支，不伪装成一个已经自动接通的程序。

这里板块就是独立流程步骤，包括同一函数／文件里的子步骤。输入、输出和替换点由普通函数／只读值／显式状态实例表达；换实现仍守住契约，不需改其他步骤。实际 old→new、拆分与裁决见 [restructure-changes.md](restructure-changes.md)。

BGR8 是蓝／绿／红三通道的 8 位图像；raw 是当前原始检测，stable 是二维平滑结果。fps 是帧率，dt 是时间间隔；K/dist 是相机内参／畸变参数；profile 是通过独立标定验证得到的跟踪参数。JSONL 表示每行一个 JSON 记录。物理角身份与屏幕角顺序分别表达，不靠数组位置互相猜。

## 主程序：从配置到一帧，再到运行结束

主链真实顺序为 main → loadConfig → runOffline/OfflineRunSession::run → validate_run → prepare_metadata → recorder.beginRun → start_input（保存配置、打开视频、构造检测器、显示探测）→ consume_input → process_frame → execute_and_record → Detector::process → 几何／取证／二维时序 → 返回记录 → 渲染／等待 → 写图与视频／submit／complete_export → 读下一帧 → finish_run。失败、缺预算和显式子集按现行分支返回，不能为了读完整路径填预算。

### 1. 主入口解析运行请求（原 A01）

解析 marker_app 允许项，装配配置、模式、输出目录与预期帧数，再进入 runOffline。

**输入／输出与替换点：**输入 argv 与配置路径；输出 AppConfig、video、ExecutionScope::Full、subset 和 purpose。替换各入口 parse_cli 时维持各自允许项／错误策略。

**难读点：**marker_app 仍有自己的 CLI；pose/tracking 和部分审计工具共用纯 parser，不能假设所有入口选项完全一致。

定位：[tools/apps/marker/main.cpp:55](../tools/apps/marker/main.cpp#L55)。

### 2. 识别各模块配置协议（原 Q01）

打开指定 YAML，识别版本、段与允许键，逐字段拒绝未知／重复／缺失输入。

**输入／输出与替换点：**输入显式配置路径；输出节点／原始 token 与 schema 约束。替换各 load_* 的文件阶段保留原模块协议和错误上下文。

**难读点：**模块 reader 的字段集合各自拥有；display 是现行段，删掉的三个旋钮、旧显示键与旧模式都明确拒绝。

定位：[src/config/detector/config.cpp:135](../src/config/detector/config.cpp#L135)。

### 3. 转换类型并保留完整整数／空值（原 Q02）

共享纯字段原语，调用者显式指定数字／数字串／可选空值策略及原错误文本。

**输入／输出与替换点：**输入 FileNode、token 与策略；输出有类型配置值。替换 config_value 原语、decimal_uint64 或 exact integer codec 时保持各模块类型边界。

**难读点：**Detector 数字节点严格，G2 普通／optional 不同，G4 允许完整数字串；共用函数不等于统一成宽松解析。

定位：[src/config/reader/value_reader.hpp:9](../src/config/reader/value_reader.hpp#L9)。

### 4. 校验各模块自己的契约（原 Q03）

由 Detector／Temporal／G2／G4 校验器分别核参数，应用总入口按原顺序组合。

**输入／输出与替换点：**输入本模块配置；输出合法值或原类型／上下文的错误。替换 validateTemporalConfig、validateDetectorConfig、validate_pose_config、validate_tracking_config 保持局部依赖。

**难读点：**合法配置可以仍有空预算；格式检查通过不是实车／控制能力已就绪。

定位：[src/config/detector/config.cpp:529](../src/config/detector/config.cpp#L529)。

### 5. 进入离线会话并复核运行组合

runOffline 创建本次独占会话，run 首先 validate_run，检查模式与显示／导出组合。

**输入／输出与替换点：**输入 AppConfig、视频／配置路径、scope/subset/purpose 和执行目标 identity；输出合法会话或原异常。替换启动校验不改变资源创建顺序。

**难读点：**这里尚未检测任何帧；验证配置合法不等于算法预算或硬件已就绪。

定位：[src/app/offline/offline_runner.cpp:74](../src/app/offline/offline_runner.cpp#L74)。

### 6. 建立身份元信息（原 A02）

prepare_metadata 记录本次输入／模型字节身份、代码来源、scope/subset 与时间配方；此后 beginRun 创建唯一运行目录。

**输入／输出与替换点：**输入已解析 AppConfig、路径、scope/subset/purpose 与代码 identity；输出独占运行目录、capture、metadata 和实例资源。替换会话初始化保持各步显式依赖与失败生命周期。

**难读点：**beginRun 早于打开 capture；读取指纹失败可能更早停止，开视频失败则会留下失败目录，不能声称所有失败都不建目录。

定位：[src/app/offline/offline_runner.cpp:74](../src/app/offline/offline_runner.cpp#L74)。

### 7. 开始记录器生命周期（原 A07）

beginRun 拒绝已有目录，写 manifest，按 level 打开记录文件；所有资源归本次 recorder。

**输入／输出与替换点：**输入 RunMetadata 和明确目录；输出 Running 记录器及文件流。替换 beginRun 的文件存储保持拒绝覆盖及失败标记。

**难读点：**打开 capture 发生在后面；启动失败目录不能冒称成功。

定位：[src/observability/recording/diagnostics_recorder.cpp:48](../src/observability/recording/diagnostics_recorder.cpp#L48)。

### 8. 保存可往返的有效配置（原 Q06）

把本次验证后的 AppConfig 写到明确路径，再按原精确 token 机制保存完整大整数。

**输入／输出与替换点：**输入 AppConfig 和输出路径；输出 effective_config.yaml。替换 writer 保持现行字段、空预算、整数精度与写失败保护。

**难读点：**运行元信息是另外的 manifest；标定／G2／G4 也有各自 writer，不能拿 AppConfig writer 包办所有协议。

定位：[src/config/detector/config.cpp:743](../src/config/detector/config.cpp#L743)。

### 9. 打开视频并读取输入规模

start_input 打开 capture，验证 fps，保留容器帧数与显式 expected 的优先级。

**输入／输出与替换点：**输入视频路径、可空期待数；输出 capture/fps/expected。替换视频初始化保持元数据未知状态。

**难读点：**显式 expected 影响完整性核验，不裁剪算法；录像时间仍不是实测曝光。

定位：[src/app/offline/offline_runner.cpp:181](../src/app/offline/offline_runner.cpp#L181)。

### 10. 构造检测器并加载模型（原 Q04）

Full scope 构造 Detector，校验自己的参数并从显式路径加载 MarkerGeometry；原图模型身份为只读值。

**输入／输出与替换点：**输入配置文件目录、资源路径与单位字段；输出只读模型、K/dist、米制点或 profile。替换资源加载须显式保留定位基准与来源。

**难读点：**默认入口路径仍有各自适配，不能说整个仓库已完全不读 cwd；F57 已将合成测试根显式传入，旧 CLI 兼容边界仍存在。

定位：[src/detection/pipeline/execution/detector.cpp:37](../src/detection/pipeline/execution/detector.cpp#L37)。

### 11. 探测并启动显示会话（原 A03）

有窗口请求时由子进程探测 GUI，再创建 DisplaySession；不可用时按原规则无窗口继续。

**输入／输出与替换点：**输入 show_window、probe 路径和既有超时；输出 active／明确降级原因。替换显示后端保持 start/show/close 生命周期归会话。

**难读点：**实际源是 app/display_probe.cpp；display_requested 与 display_active 分开记录，不能把未显示写成已显示。

定位：[src/app/display/display_session.cpp:72](../src/app/display/display_session.cpp#L72)。

### 12. 逐帧读取并建立回放时间轴（原 A04）

读取每帧，执行显式 subset 筛选，为实际处理帧计算 frame_id 和基于 fps 的微秒时间。

**输入／输出与替换点：**输入 capture、fps、id 与 subset；输出 FrameInput 和读取耗时。替换读帧／时间适配保持编号、溢出检查及来源。

**难读点：**录像时间来源仍 Unknown，不能写成真实曝光；diagnostics 抽样不裁剪检测，但显式 execution subset 会。

定位：[src/app/offline/offline_runner.cpp:239](../src/app/offline/offline_runner.cpp#L239)。

### 13. 选择执行范围并交接诊断请求（原 A05）

将 selected／timing／level 作为观察请求，完整 scope 走 execute_and_record，审计 scope 显式传 runDecodePipeline。

**输入／输出与替换点：**输入帧、ExecutionScope 与 DiagnosticsRequest；输出一次算法执行的参数。替换编排只调整步骤实现，不能让 observer 是否存在决定算法范围。

**难读点：**DiagnosticsRequest.scope 是记录标签；真正范围是独立函数参数。Full 公共 Detector 继续保留原预算门控。

定位：[src/app/offline/offline_runner.cpp:275](../src/app/offline/offline_runner.cpp#L275)。

### 14. 进入一次检测与诊断交接（原 A06）

内部桥完成 prepare→公共 process→take，带回结果、记录、异常和簿记耗时。

**输入／输出与替换点：**输入 Detector、FrameInput、request；输出 RecordedExecution。替换桥时保留错序守卫、异常先保存记录再重抛和 process_total 的 public_call 边界。

**难读点：**FrameRecord 的 StageDetails 是观察快照；未执行和关闭计时不记成零耗时，诊断失败也不能更改数值流程。

定位：[src/detection/pipeline/execution/detector.cpp:193](../src/detection/pipeline/execution/detector.cpp#L193)。

### 15. 核验当前帧和序列（原 D01）

先拒绝空图、错误像素格式或不合法序列，再处理尺寸改变和缺预算；失败分支按现行规则清历史或返回 NOT_READY。

**输入／输出与替换点：**输入 FrameInput 和该 Detector 的上一帧标记／尺寸；输出可继续的当前帧或带原因的 FrameResult。替换点为帧校验及其显式上一帧参数。

**难读点：**构造时校验 DetectorConfig 并加载模型；process 不负责补空预算，NOT_READY 与没检出不是同一状态。

定位：[src/detection/pipeline/execution/detector.cpp:51](../src/detection/pipeline/execution/detector.cpp#L51)。

### 16. 生成工作图与双向坐标映射（原 D02）

把当前 BGR8 原图缩到配置尺寸，保留只读原图视图、真实比例和像素中心映射。

**输入／输出与替换点：**输入帧与 PreprocessConfig；输出 PreparedFrame。替换 preprocess 时须继续提供原图来源、scale_x/scale_y 与 workToOriginal/originalToWork 契约。

**难读点：**image_ 此时仍是 BGR8；工作图的二值化在 提取带身份的白片，原图阈值另有明确来源，不能混用。

定位：[src/detection/preprocess/preprocess.cpp:11](../src/detection/preprocess/preprocess.cpp#L11)。

### 17. 提取带身份的白片（原 D03）

在工作图上阈值化并提取白片轮廓，给每片建立 component_id。

**输入／输出与替换点：**输入 PreparedFrame.image_ 与 GeometryConfig；输出 vector<WhiteComponent>。替换点为 extractWhiteComponents，保留工作坐标、轮廓遍历约定和来源 ID。

**难读点：**component_id 是身份，不是 vector 下标；下游按 ID 查找，不能依赖排列巧合。

定位：[src/detection/geometry/observation/geometry_observation.cpp:43](../src/detection/geometry/observation/geometry_observation.cpp#L43)。

### 18. 把轮廓变成形状与拓扑观察（原 D04）

对每片做多尺度近似，提取凹转折与 L／小片等可检验候选。

**输入／输出与替换点：**输入带 ID 的白片和观察参数；输出 ShapeObservation 列表。替换 observeShapes 或其拓扑函数时保留原组件来源与实际测得顶点。

**难读点：**候选标签还不是最终模型身份；观察位置、组件 ID、source_index 的含义各不相同。

定位：[src/detection/geometry/observation/geometry_observation.cpp:124](../src/detection/geometry/observation/geometry_observation.cpp#L124)。

### 19. 枚举三片 L 与模型的对应（原 D05）

从本帧 L 候选选三片，枚举与 L0/L2/L3 的对应关系，并记录资源截断。

**输入／输出与替换点：**输入 ShapeObservation、模型与搜索参数；输出三片对应排列及截断状态。替换点为 generateThreeLAssignments，输出须完整表达竞争。

**难读点：**当前名称已修正；只枚举三 L，不是六片全排列，也不能拿最大的三片暗中当唯一。

定位：[src/detection/geometry/parent/geometry_matcher.cpp:104](../src/detection/geometry/parent/geometry_matcher.cpp#L104)。

### 20. 拟合父仿射假设（原 D06）

对每组对应再枚举实测锚点元组，拟合父变换并组装带来源的假设。

**输入／输出与替换点：**输入三 L 观察、模型与预算；输出 generated GeometryBatch。替换拟合／组装函数时显式传锚点和 assignments，buildGeometryHypothesis 只负责组装。

**难读点：**每个可计算元组都计入原搜索预算；截断表示尚有未排除解释，不能在下游抹掉。

定位：[src/detection/geometry/parent/geometry_matcher.cpp:257](../src/detection/geometry/parent/geometry_matcher.cpp#L257)。

### 21. 独立复核父假设（原 D07）

先查绑定和变换，再用同一投影产物检查残差、面积及完整性，保持原拒绝先后。

**输入／输出与替换点：**输入 generated batch、模型、当前 components 和 GeometryConfig；输出 validated batch。替换 validateGeometryBatch 或其中度量函数时保留原绑定来源、拒绝原因和降级规则。

**难读点：**这是父假设检查，不等于 独立验证原图角证据 的最终原图角证据验证；两者不能互相代替。

定位：[src/detection/geometry/validation/geometry_validation.cpp:196](../src/detection/geometry/validation/geometry_validation.cpp#L196)。

### 22. 测量缺片候选（原 D08）

先做廉价面积筛查，再测缺片与当前白片的形状、方向、边界和拓扑关系。

**输入／输出与替换点：**输入白片、模型片、父仿射、参照面积及工作尺度；输出 AssignmentMatchMetrics。替换点为 measureAssignmentArea／measureAssignmentMatch，已算面积通过可选值显式交接。

**难读点：**可选预计算值只用于同一输入；独立入口仍自己算面积并防御坏输入，不能把未知测量当零。

定位：[src/detection/geometry/completion/assignment_match_metrics.hpp:122](../src/detection/geometry/completion/assignment_match_metrics.hpp#L122)。

### 23. 完成有界的一对一绑定（原 D09）

沿父绑定枚举 M1/S1a/S1b 的可行组合，避免一片多用并保留竞争／资源截断。

**输入／输出与替换点：**输入 validated batch、components、模型及 AssignmentCompletionConfig；输出 completed batch。替换搜索时保留候选、扩展、输出三类上限及截断标记的含义。

**难读点：**Geometry 范围在此返回；GeometryStop::Validated 则更早在 独立复核父假设 返回。诊断 selected 只决定详情，不裁剪正式算法。

定位：[src/detection/geometry/completion/geometry_assignment_completion.cpp:33](../src/detection/geometry/completion/geometry_assignment_completion.cpp#L33)。

### 24. 进入当前帧解码与候选循环

decodeStage 检查完成标记与原图预算，建立本次调用的 OriginalContourIndex，逐个核父假设完整性和六片绑定。

**输入／输出与替换点：**输入完成的 GeometryBatch、PreparedFrame、显式 components／模型／角预算；输出待取证候选或明确失败。替换 decode 编排保持 unresolved 竞争及当前帧身份。

**难读点：**原图分割是惰性的，第一次 observe 才真正执行；不是每个候选都重扫整图。

定位：[src/detection/pipeline/decode/decode_stage.cpp:57](../src/detection/pipeline/decode/decode_stage.cpp#L57)。

### 25. 按物理角找到片段和邻边（原 D11）

按只读角身份表找到 P0…P3 各自的片、顶点和两条邻边，将图纸边映到原图作取证参照。

**输入／输出与替换点：**输入完成绑定、当前轮廓来源、模型与映射；输出每个物理角的 ModelEdges 和来源身份。替换绑定解析时仍只消费 model_corner_contract 与显式输入。

**难读点：**预测邻边只限定取证方向／位置；没有实测边就返回失败，不用预测交点补角。

定位：[src/detection/corners/measurement/corner_resolver.cpp:74](../src/detection/corners/measurement/corner_resolver.cpp#L74)。

### 26. 查询该片的当前原图轮廓（原 D10）

由 decode 局部索引惰性分割原图，把工作组件关联到真实原图轮廓。

**输入／输出与替换点：**输入原图／映射、帧号、白片与原图预算；输出 OriginalContourResult。替换点为 OriginalContourSource 的只读 observe 回调；resolver 只知道 frame_id 与查询契约。

**难读点：**缓存生命周期止于一次 decode；回调不能外逃，也不能用父变换画出的边冒充实拍轮廓。

定位：[src/detection/corners/observation/corner_observation.cpp:37](../src/detection/corners/observation/corner_observation.cpp#L37)。

### 27. 进入有序路线尝试（原 D13）

fitObservedEdgePair 进入固定 13 路表；每条已启用路线内部先取支持、拟合，再配对选择，首次有 evidence 返回。

**输入／输出与替换点：**输入支持弧、配置与 EdgePairFitStrategy；输出 EdgePairFitResult／失败原因，可选 trace。替换 fitFor／路线实现时保留路序、成功判据及 trace 契约。

**难读点：**Fast 第 8/9 路复用同输入第 2/4 路结果，但照常重放逻辑 stage；完整清单、零命中保留裁决见 瘦身修改报告。

定位：[src/detection/corners/fitting/corner_edge_fit.cpp:744](../src/detection/corners/fitting/corner_edge_fit.cpp#L744)。

### 28. 在当前路线内枚举连续支持弧（原 D12）

从当前原图轮廓生成路线所需支持弧，检查连续性、端点范围和有效长度。

**输入／输出与替换点：**输入轮廓、ModelEdges、角配置与路线端点策略；输出有序 SupportRange／SupportRecord。替换弧枚举或 supportFor 时保留有序像素身份与连续连接语义。

**难读点：**全端点枚举仍按原规则去重并恢复候选顺序；不能只按空间距离把两段孤立线拼成角。

定位：[src/detection/corners/fitting/corner_edge_fit.cpp:262](../src/detection/corners/fitting/corner_edge_fit.cpp#L262)。

### 29. 拟合本路线支持边并计量残差

fitFor 对当前支持序列估计指定线模型，输出物理直线、周期参数和原始／模型残差。

**输入／输出与替换点：**输入 SupportRecord、LineFitModel 与现行预算；输出 FittedSupport 或失败。替换拟合步骤保留 raw 与模型残差各自含义。

**难读点：**同输入缓存只活在当前 fitter；独立验证器仍从支撑像素重算，不复用这里的数值答案。

定位：[src/detection/corners/fitting/corner_edge_fit.cpp:346](../src/detection/corners/fitting/corner_edge_fit.cpp#L346)。

### 30. 组装实测交点与完整证据（原 D14）

对通过相邻连接／交角／外推等检查的两边取交点，再由 resolver 写入物理角和帧／片身份。

**输入／输出与替换点：**输入两边拟合、连接弧与模型身份；输出 CornerMeasurement 的四个 double 物理角及逐角 CornerEvidence。替换 pairGeometry／select 时继续交付全部模型元数据与支撑像素。

**难读点：**stable_id 是证据来源标识，不是时序 Track ID；四角缺一即拒绝该假设。

定位：[src/detection/corners/fitting/corner_edge_fit.cpp:559](../src/detection/corners/fitting/corner_edge_fit.cpp#L559)。

### 31. 完成四个物理角的帧与来源身份

resolver 将逐角 evidence 加上 frame_id、component_id、模型边／顶点和 stable_id，并形成四角测量。

**输入／输出与替换点：**输入本帧逐角实测结果；输出 CornerMeasurement。替换组装保留全部来源字段，失败角不能由预测补齐。

**难读点：**前面的绑定→轮廓→路线→交点在每个角内部执行，路线中支持／拟合也按需交织；不是先算完所有路线再选。

定位：[src/detection/corners/measurement/corner_resolver.cpp:119](../src/detection/corners/measurement/corner_resolver.cpp#L119)。

### 32. 求屏幕角序并交付排序结果（原 D15）

把同一组 double 物理角归入 LT/RT/RB/LB，保留物理到屏幕的映射与平局信息。

**输入／输出与替换点：**输入四物理角与 CornerConfig；输出 ScreenOrderResult。替换点为 orderScreenCorners；resolver 的可选输出把同一结果显式交给几何和语义步骤。

**难读点：**屏幕左上不等于物理 P0；double 排序缓存只适用于这份数值，不能直接复用于 float 发布后。

定位：[src/detection/corners/ordering/screen_order.cpp:660](../src/detection/corners/ordering/screen_order.cpp#L660)。

### 33. 独立验证原图角证据（原 D16）

复核四边形、图像边界以及每个角的支撑／模型／残差／连接证据。

**输入／输出与替换点：**输入 CornerMeasurement、同组 ScreenOrder、原图尺寸与预算；输出有效性及拒绝原因。替换 validateDetectionGeometry／validateCornerEvidence 时保留独立复算和完整失败信息。

**难读点：**只能共享身份和协议常量，不能把 fitter 缓存的数值结论当验证结果；frame_id 也须与本帧一致。

定位：[src/detection/corners/validation/detection_validator.cpp:270](../src/detection/corners/validation/detection_validator.cpp#L270)。

### 34. 处理竞争与方向唯一性（原 D17）

对全部有效测量及搜索截断状态判断几何是否一致，保留真实测量并单独判定方向是否唯一。

**输入／输出与替换点：**输入完整候选、search_truncated、配置及可选同组排序；输出 SemanticResolution。替换 resolveSemantics 时须消费完整竞争集合，不隐藏失败竞争。

**难读点：**decode 在进入这里前还检查 unresolved 竞争；不能丢掉无法排除的假设后将剩下一个强行叫唯一，也不平均多个角。

定位：[src/detection/corners/semantics/semantic_resolver.cpp:333](../src/detection/corners/semantics/semantic_resolver.cpp#L333)。

### 35. 转 float 后再发布当前检测（原 D18）

将保留测量转换为公开 float 四角，重新验证／排序，生成 bbox 和可空 orientation。

**输入／输出与替换点：**输入真实 double 测量、方向唯一性、原图尺寸及配置；输出可发布 Detection 或失败。替换发布适配时保持公开结构与 float 后的几何复核。

**难读点：**所有待发布候选先成功才提交；失败不能偷偷删掉竞争者。输出是 raw 当前检测，随后才进入 T 链。

定位：[src/detection/corners/publication/detection_publication.cpp:10](../src/detection/corners/publication/detection_publication.cpp#L10)。

### 36. 进入二维时序与结果装配（原 T06）

finalizeDecodedFrame 先处理非法／未就绪和 status/payload 矛盾，再调用独立 temporal 与 display 实例；下几步在此调用内部发生。

**输入／输出与替换点：**输入 DecodeStageResult、stamp、size 及两个显式状态实例；输出 FrameResult。替换 finalizeDecodedFrame 时保持公开字段的来源和非法分支清理顺序。

**难读点：**detections 仍是原始当前测量；tracks 是二维平滑结果，不等于 G4 的三维 TargetTrack。

定位：[src/detection/temporal/assembly/stabilize_stage.cpp:8](../src/detection/temporal/assembly/stabilize_stage.cpp#L8)。

### 37. 核验时序输入并维护重置边界（原 T01）

检查本帧所有 raw、标记和尺寸，再处理历史过期或输入变化。

**输入／输出与替换点：**输入 raw 列表、FrameStamp、原图尺寸及本实例历史；输出可继续的当前集合或 INVALID_INPUT。替换输入校验须显式传上一标记，状态仍归实例。

**难读点：**这里再次防御是独立步骤入口的契约；空当前帧不制造历史几何，且会清平滑历史。

定位：[src/detection/temporal/stabilization/temporal_stabilizer.cpp:106](../src/detection/temporal/stabilization/temporal_stabilizer.cpp#L106)。

### 38. 选择当前跟踪对象（原 T02）

用中心距离和面积比例找历史匹配；恰好一个才延续，没有唯一匹配则选当前最大 raw 并停止沿用平滑。

**输入／输出与替换点：**输入当前 raw、reference_ 与关联参数；输出 current_index、匹配／歧义信息。替换关联段时显式给这些产物，不能靠原因字符串反向控制流程。

**难读点：**多个历史匹配不会偷偷挑最近者来平滑；仍可显示当前最大真实检测，但不能说关联唯一。

定位：[src/detection/temporal/stabilization/temporal_stabilizer.cpp:156](../src/detection/temporal/stabilization/temporal_stabilizer.cpp#L156)。

### 39. 确定角点对应关系（原 T03）

在需要且允许平滑时，按已知物理映射或未知方向的循环候选寻找唯一角对应。

**输入／输出与替换点：**输入当前／历史 raw、各自 orientation 与对应预算；输出 current_to_previous 或明确歧义。替换 resolveCornerCorrespondence 保留映射方向及歧义输出。

**难读点：**仅在 选择当前跟踪对象 唯一关联且已有平滑历史时调用；未知方向不会因此变成已知物理方向。

定位：[src/detection/temporal/correspondence/temporal_geometry.cpp:10](../src/detection/temporal/correspondence/temporal_geometry.cpp#L10)。

### 40. 按真实时间平滑并复核（原 T04）

按实际 dt 计算权重，平滑对应角并复核偏离、凸性、float 与排序；失败回退当前 raw。

**输入／输出与替换点：**输入当前角、对应映射、历史 stable、dt 与 TemporalConfig；输出本帧 Track、诊断及下一历史。替换平滑段／validateStableCorners 时保留当前帧回退和显式原因。

**难读点：**零 dt、歧义、超偏离都不发布预测角；raw 不被覆盖。TemporalCause 是机器原因，旧 reason 文字只作兼容记录。

定位：[src/detection/temporal/stabilization/temporal_stabilizer.cpp:195](../src/detection/temporal/stabilization/temporal_stabilizer.cpp#L195)。

### 41. 更新独立文字保持（原 T05）

根据可选的当前文字样本与调用年龄，返回带来源帧／age／is_held 的 DisplayState。

**输入／输出与替换点：**输入 optional<SemanticDisplaySample>、FrameStamp 与本实例文字历史；输出 optional<DisplayState>。替换点为 DisplayHistory::update，不能把它改成几何观测来源。

**难读点：**当前 finalizeDecodedFrame 实际传入 std::nullopt，没有接入文字识别样本；新实例通常持续无 display_state。原表的“当前文字”是接口能力，不是现有调用事实。

定位：[src/detection/temporal/display/display_history.cpp:22](../src/detection/temporal/display/display_history.cpp#L22)。

### 42. 返回公开帧结果并取走独立记录

装配器保留 raw.detections，另放 tracks/display_state；Detector 结束本帧记录后 execute_and_record take，异常先交记录再重抛。

**输入／输出与替换点：**输入解码值、时序结果与请求；输出 FrameResult、FrameRecord、可空异常及簿记时间。替换适配保持三种来源与 public_call 计时边界。

**难读点：**raw 不被 stable 覆盖；当前文字保持输入为空，不能写成已经有文字识别器。

定位：[src/detection/pipeline/execution/detector.cpp:193](../src/detection/pipeline/execution/detector.cpp#L193)。

### 43. 记录捕获开销与结果指纹

update_observations 记首帧身份、capture 时间和非 Geometry 范围的公开结果指纹。

**输入／输出与替换点：**输入本帧 result/record/read_elapsed；输出更新后的记录及运行级指纹。替换观察簿记不反向控制数值流程。

**难读点：**public_call 已结束，外部簿记不混入检测总时间。

定位：[src/app/offline/offline_runner.cpp:336](../src/app/offline/offline_runner.cpp#L336)。

### 44. 适配渲染输入并生成画布（原 A08）

从只读快照提取原图点线、检查 evidence 一次，再叠 raw／stable／候选等图层到新画布。

**输入／输出与替换点：**输入原图、FrameResult、FrameRecord 与 RenderConfig；经 RenderInput 输出新 overlay／事件。替换适配器或图层时保持原图坐标及只读输入。

**难读点：**baseline 跳过画布；记录字段和渲染适配仍有明确协议，渲染不得回写原图或算法结果。

定位：[src/app/rendering/debug_renderer.cpp:26](../src/app/rendering/debug_renderer.cpp#L26)。

### 45. 显示、等待并读取用户停止（原 A09）

对可用会话显示画布，处理停止按键及 playback 等待，记录真实可视化／等待耗时。

**输入／输出与替换点：**输入 overlay、session 与 playback 配置；输出继续／用户停止、显示状态和耗时。替换 UI 等待保持停止原因及时间归账边界。

**难读点：**用户停止会影响输入完整性；窗口中途降级后不能重复记入上一次显示耗时。

定位：[src/app/display/display_session.cpp:96](../src/app/display/display_session.cpp#L96)。

### 46. 输出视频／图像并补 export 耗时（原 A10）

先按请求写选中证据图／每个执行帧视频，再 submit 记录并 complete_export。

**输入／输出与替换点：**输入 record、overlay、frame_id 与输出请求；输出文件、侧表耗时及完成身份。替换 VideoExporter 或 recorder 编码保持 submit→complete_export 顺序。

**难读点：**视频不由 selected 决定；不完整运行最后标 partial。显式 subset 情况只导出实际执行帧。

定位：[src/app/offline/offline_runner.cpp:397](../src/app/offline/offline_runner.cpp#L397)。

### 47. 提交所有帧并补本帧导出时间

export_frame 在实际写图／视频后 submit，再按本帧身份 complete_export；统计涵盖所有提交帧，详情按 selected。

**输入／输出与替换点：**输入 FrameRecord、selected、导出身份；输出 JSONL、统计及 export 侧表。替换存储保持单调帧和重复完成检查。

**难读点：**抽样不决定是否处理／导出视频；不能把未测耗时当零。完成后回到读帧步骤。

定位：[src/app/offline/offline_runner.cpp:305](../src/app/offline/offline_runner.cpp#L305)。

### 48. 判定覆盖并收尾资源（原 A11）

用已读帧数／预期／subset 待完成项／用户停止求 RunOutcome，再关闭并读回核验视频。

**输入／输出与替换点：**输入真实计数、可空 expected 和停止原因；输出 coverage_verified、incomplete、reason 及完成／partial 视频。替换 evaluate_input_completion 保留 unknown 与 incomplete 的区别。

**难读点：**编码器收尾在 recorder Running 期间进行；不能先写成功 summary 再发现视频失败。

定位：[src/app/offline/offline_runner.cpp:426](../src/app/offline/offline_runner.cpp#L426)。

### 49. 输出只读摘要与运行结论（原 A12）

显式传 RunOutcome／RunOverhead 给 finishRun，再写 summary、manifest、补充导出报告与控制台。

**输入／输出与替换点：**输入只读累计统计、运行结论、外部开销和 provenance；输出文件／终端及退出码。替换各 writer 保留公开编码与失败生命周期。

**难读点：**environment 仍编码兼容字段，但不作内部状态暗道；summary() 只读，未知覆盖不能冒称全量通过。

定位：[src/observability/reporting/run_report.hpp:19](../src/observability/reporting/run_report.hpp#L19)。

## 位姿程序分支：从当前观测到位姿文件

入口单独是 pose_app；读取原分辨率图并构造 CaptureStamp，先调用完整 Detector。随后对每个 raw MARK 做下面的点对与求解；Tag 分支在同帧 MARK 记录之后执行，再复用同一平面求解函数。G4 不会自动作为先验暗接回这里。

### 50. 把 MARK raw 转成物理点对（原 P01）

核对帧／时间／状态／索引，使用 orientation 把屏幕角转回 P0…P3，再配上米制图纸点。

**输入／输出与替换点：**输入 FrameResult.detections、检测序号、原图尺寸、CaptureStamp 及可选 PosePrior；输出四组点对或明确 PoseStatus。替换 MARK 适配时禁止用 tracks／held。

**难读点：**pose_app 先执行完整 Detector.process，但 estimate_mark 只读取 raw；方向缺失即 CorrespondenceMissing，不把屏幕序当物理序。

定位：[src/pose/estimation/pose_estimator.cpp:82](../src/pose/estimation/pose_estimator.cpp#L82)。

### 51. 检查平面求解前提与预算（原 P05）

验证 stamp、相机、图像大小、凸四边形与有限 z=0 物点，再检查已存在的面积预算。

**输入／输出与替换点：**输入四组点对、K/dist、stamp、target ID 与可选 prior；输出可求解输入或现行错误状态。替换前置校验保留数学检查与预算空值。

**难读点：**缺重投影预算仍可能计算候选供审计，然后返回 BudgetMissing；不能把空预算补成经验值。

定位：[src/pose/estimation/pose_estimator.cpp:158](../src/pose/estimation/pose_estimator.cpp#L158)。

### 52. 枚举平面姿态候选（原 P06）

调用 SOLVEPNP_IPPE，保留求得的全部旋转／平移候选。

**输入／输出与替换点：**输入合法 3D↔2D 点对和相机参数；输出候选 rvec/t_m。替换求解器时保留多解及逐候选复核输入。

**难读点：**不是拿返回的首解或最小误差解直接宣布 Unique；G4 预测也没有在当前应用中自动作为 prior 接回这里。

定位：[src/pose/estimation/pose_estimator.cpp:217](../src/pose/estimation/pose_estimator.cpp#L217)。

### 53. 独立评价每个姿态（原 P07）

把候选重新投影到原始像点，检查正深度并计算误差指标。

**输入／输出与替换点：**输入候选姿态、原点对与 K/dist；输出 PoseCandidate 的 metrics_valid、误差、深度及原因。替换 evaluate_pose 保留独立重投影。

**难读点：**rvec 是轴角旋转向量，t_m 单位米；未测指标不能当零误差通过。

定位：[src/pose/validation/pose_geometry.cpp:86](../src/pose/validation/pose_geometry.cpp#L86)。

### 54. 按现有约束筛候选（原 P08）

依次用已存在的重投影预算、安装约束和可信 prior 更新每个候选的 eligible 与 reasons。

**输入／输出与替换点：**输入复核候选、SelectionConfig、Budgets 和显式 prior；输出完整标记过的候选列表。替换单个谓词时保留其独立输入与失败原因。

**难读点：**硬件确认位不充当数值求解总开关；无可用 prior 时明确忽略，不造一个默认追踪器。

定位：[src/pose/estimation/pose_estimator.cpp:232](../src/pose/estimation/pose_estimator.cpp#L232)。

### 55. 分类零解／唯一／歧义（原 P09）

统计合格候选；零个 NoSolution，多个 Ambiguous，仅一个才进入精修与发布。

**输入／输出与替换点：**输入完整候选及 eligible 标记；输出 PoseStatus 和可选 selected_index。替换分类逻辑时保留全部失败候选证据及公开状态。

**难读点：**BudgetMissing 先于这一步返回；Unique 仅指当前数值约束下唯一，不等于硬件或控制资格通过。

定位：[src/pose/estimation/pose_estimator.cpp:258](../src/pose/estimation/pose_estimator.cpp#L258)。

### 56. 尝试精修并保留原合法解（原 P10）

对唯一解可选运行 LM 精修，再用同样约束复核；精修失败或变坏时保留原合法解。

**输入／输出与替换点：**输入唯一候选、原点对、相机与 refine_lm 开关；输出 refined 或原解及相关误差。替换精修段须保留复核与回退契约。

**难读点：**不能因优化器抛异常就丢掉前面已验证的原解，也不能凭误差下降跳过安装／prior 条件。

定位：[src/pose/estimation/pose_estimator.cpp:273](../src/pose/estimation/pose_estimator.cpp#L273)。

### 57. 组装姿态与现行资格（原 P11）

在求解结果上补 MARK／Tag 来源、索引与现行 control_usable 资格。

**输入／输出与替换点：**输入求解状态、姿态、stamp 及硬件／时间资格；输出 PoseObservation（Tag 外包 ExitObservation）。替换输出适配保持公开字段、可空协方差与现行判定。

**难读点：**原表写 G2PoseObservation 是概念称呼，实际公开类型是 mark::pose::PoseObservation；本次没有新增 G2→G4 隐式连接。

定位：[src/pose/estimation/pose_estimator.cpp:133](../src/pose/estimation/pose_estimator.cpp#L133)。

### 58. 输出 G2 记录与分析入口（原 P12）

逐帧分别记录 MARK 和 Tag 的状态、候选、选择与原因，收尾给出来源／完整性摘要。

**输入／输出与替换点：**输入 PoseObservation／ExitObservation 与明确输出目录；输出 observations.yaml、配置副本及 summary.yaml。替换 writer 保留现行 YAML 和来源，分析工具独立读取其输入。

**难读点：**原步骤表笼统写 JSONL，pose_app 当前实际写 YAML；没有把这份文件偷偷改成新的三维跟踪输入协议。

定位：[tools/apps/pose/pose_main.cpp:48](../tools/apps/pose/pose_main.cpp#L48)。

### 59. 适配 Tag 灰度输入（原 P02）

从原分辨率 BGR8 生成灰度，构造指向有效灰度存储的 C 图像视图。

**输入／输出与替换点：**输入原图与 CaptureStamp；输出 gray／image_u8_t 临时视图。替换输入适配保持真实 Mat.step、尺寸与存储生命周期。

**难读点：**不使用 生成工作图与双向坐标映射 工作图，也不隐式缩放相机 K；视图在当前检测调用内有效。

定位：[src/pose/tag/tag_detector.cpp:55](../src/pose/tag/tag_detector.cpp#L55)。

### 60. 检测并解码 Tag（原 P03）

调用每实例 AprilTag 后端，复制 ID／角点／质量并检查白名单、质量与同帧重复 ID。

**输入／输出与替换点：**输入灰度视图和 TagConfig；输出含失败原因的 TagObservation 列表。替换后端继续提供相同角身份及解码证据。

**难读点：**不能只保留成功 ID：重复／缺质量预算必须留下诊断，原生 detector/family 不跨实例共享可变状态。

定位：[src/pose/tag/tag_detector.cpp:66](../src/pose/tag/tag_detector.cpp#L66)。

### 61. 用 Tag 尺寸构造米制点对（原 P04）

复核 Tag 质量及批准尺寸，按该后端角定义构造四个平面物点，再调用公共平面求解。

**输入／输出与替换点：**输入 TagObservation、原图尺寸与 PoseEstimator 配置快照；输出点对／拒绝状态，后续再包装 ExitObservation。替换 estimate_exit 的输入适配保持 ID→side_m 显式来源。

**难读点：**后端角顺序直接对应物点，不重排成屏幕 LT/RT/RB/LB；未知尺寸不猜。config() 已按值隔离 K/dist。

定位：[src/pose/tag/tag_detector.cpp:106](../src/pose/tag/tag_detector.cpp#L106)。

Tag 点对通过后回到前述平面求解步骤并写同一份候选协议；pose_app 最后写 summary.yaml。原始检测、平面多解、Unique 与 control_usable 的区别保持；不能由二维 tracks 或 held 显示造新测量。

## 三维跟踪程序分支：显式帧文件到冻结输出

入口是 tracking_app：先读配置／profile，再读明确协议的帧。文件中的 TypedMemory 会降为 Synthetic；不会自动调用 MARK／G2，也不继承实车资格。下面按真实分支顺序展开。

### 62. 核验协方差数学有效性（原 Q05）

用低层只读数学函数检查有限、对称与正定条件，供配置与 filter 各自入口使用。

**输入／输出与替换点：**输入 R／camera_mse_m2 矩阵；输出 bool 或调用者自己的错误。替换 positive_definite 保留数值规则及误差上下文。

**难读点：**config 不再包含私有 Kalman 头；共享数学函数不意味着模块必须依赖滤波器实现。

定位：[src/math/covariance/covariance_validation.cpp:6](../src/math/covariance/covariance_validation.cpp#L6)。

### 63. 读取明确来源的跟踪帧（原 K01）

读取现行跟踪帧协议或旧 G2 记录，保留整数／stamp／来源，交给 tracking_app 逐帧处理。

**输入／输出与替换点：**输入文件路径与协议内容；输出 TrackingFrame 列表。替换 codec／旧版适配时保持完整整数与 legacy 来源含义。

**难读点：**tracking_app 会把文件中的 TypedMemory 降为 Synthetic；文件回放不会继承内存实车资格，且不直接调用 Detector。

定位：[src/tracking/io/tracking_io.cpp:200](../src/tracking/io/tracking_io.cpp#L200)。

### 64. 创建候选事务并核帧（原 K02）

复制可变 TrackerState，核验 stamp／上下文／顺序；只读配置不随帧复制。

**输入／输出与替换点：**输入 TrackingFrame、旧实例状态和 const TrackingConfig；输出 candidate 与本帧结果，接受后才提交。替换 step／advance 的事务边界须保留拒绝与异常回滚。

**难读点：**advance 内会生成快照，返回且 input_accepted 后 step 才提交；原表 决定生命周期并提交已接受候选→生成冻结输出快照与资格 不是代码字面顺序。

定位：[src/tracking/lifecycle/target_tracker.cpp:356](../src/tracking/lifecycle/target_tracker.cpp#L356)。

### 65. 提取当前唯一的三维测量（原 K03）

核对当前 Unique 姿态及完整候选证据，用 profile 或显式 position_noise 绑定位置误差。

**输入／输出与替换点：**输入本帧 PoseObservation、stamp、profile／sidecar；输出 PositionMeasurement 列表和事件。替换 one_measurement／adapt_measurements 时保留当前样本身份与重复检测 ID 拒绝。

**难读点：**不猜 covariance_se3 数组排列来造 R；Unique 标签本身还不够，选中证据也要合法。

定位：[src/tracking/measurement/measurement_adapter.cpp:81](../src/tracking/measurement/measurement_adapter.cpp#L81)。

### 66. 核对坐标、时间域及误差来源（原 K04）

需要世界坐标时使用同曝光 CameraTransform，并传播误差；每帧 time_bounded 只算一次。

**输入／输出与替换点：**输入相机位置／R、同曝光变换与时间元数据；输出指定坐标测量、资格及必要事件。替换坐标／time_bounded／time_augmented_noise 函数时保持显式数据和单位。

**难读点：**实际先适配坐标，再对 live 轨迹预测，之后增广测量 R；camera_relative 的速度包含相机自运动，不能称世界速度。

定位：[src/tracking/measurement/measurement_adapter.cpp:131](../src/tracking/measurement/measurement_adapter.cpp#L131)。

### 67. 对已有轨迹做常速度预测（原 K06）

在合法 dt 与已标定过程域内推进位置／速度和协方差，超域先走丢失规则。

**输入／输出与替换点：**输入旧 x/P、dt、加速度功率谱与可选时间界；输出预测 x/P。替换 CvFilter::predict 保留数学防御、单位和事务回滚。

**难读点：**只在 live 分支对实时状态预测；预测输出不是新的图像测量，坏数学不能只降资格后继续算。

定位：[src/tracking/filter/kalman_cv.cpp:122](../src/tracking/filter/kalman_cv.cpp#L122)。

### 68. 计算创新并选择唯一关联（原 K07）

对候选位置计算创新评分，恰好一个通过且时间区间可分辨才更新。

**输入／输出与替换点：**输入同一预测 filter 与每个 z/R；输出 InnovationResult（残差、S、R、score、owner/revision）及选中关联。替换关联时显式传所选创新，不借字符串暗传。

**难读点：**评分使用 profile 的 bootstrap／association 门限；多候选通过会 coast，不偷偷挑最小值；缓存仅对同实例同版本有效。

定位：[src/tracking/filter/kalman_cv.cpp:155](../src/tracking/filter/kalman_cv.cpp#L155)。

### 69. 按分支初始化、两点确认或重建（原 K05）

无 live 且恰好一个测量时 create；未确认轨迹在后续唯一匹配中 bootstrap，单测量失配可进入两点 reseed。

**输入／输出与替换点：**输入当前／待定测量、合法 dt、profile 与候选 state；输出位置先验、新 track_id、confirmed 或 pending。替换 create／bootstrap／reseed 保留身份与时间条件。

**难读点：**不是每帧都先初始化再预测：create、bootstrap、reseed 位于不同分支，重建会产生新 ID。

定位：[src/tracking/lifecycle/target_tracker.cpp:75](../src/tracking/lifecycle/target_tracker.cpp#L75)。

### 70. 校正已确认滤波状态（原 K08）

已确认且唯一关联时消费同状态创新，更新 x/P；未确认时改走两点 bootstrap。

**输入／输出与替换点：**输入预测状态与选中 InnovationResult；输出校正状态／协方差。替换 correct 保留独立 z/R 入口的校验和过期／错实例防御。

**难读点：**不能在 predict／correct 之后再复用旧创新；本次复用的是原来在同状态重复计算的结果。

定位：[src/tracking/filter/kalman_cv.cpp:181](../src/tracking/filter/kalman_cv.cpp#L181)。

### 71. 决定生命周期并提交已接受候选（原 K09）

没有更新时转 Coasting，满足现行失效条件时 Lost；本帧 advance 返回后由 step 决定提交候选。

**输入／输出与替换点：**输入更新标记、gap／不确定性及 candidate；输出状态／事件及最终实例状态。替换生命周期段保持 Lost 清载荷、ID 单调及 input_accepted 提交条件。

**难读点：**快照 生成冻结输出快照与资格 在 advance 返回前生成，提交在其后；不能写成“先提交再取快照”。数学异常会返回 NO_COMMIT 事件。

定位：[src/tracking/lifecycle/target_tracker.cpp:322](../src/tracking/lifecycle/target_tracker.cpp#L322)。

### 72. 生成冻结输出快照与资格（原 K10）

按候选当前状态生成 TargetTrack，保留现行速度可空与 qualified_for_g5 条件。

**输入／输出与替换点：**输入候选状态、profile、时间与硬件来源；输出 TrackingResult 内的轨迹快照。替换 snapshot 只可保持既有公开语义，本次 F25 不改。

**难读点：**标准差保持原短路求值顺序；没有新增 UncertaintySummary。数值轨迹存在不代表 qualified，为合成回放也不升级资格。

定位：[src/tracking/lifecycle/target_tracker.cpp:95](../src/tracking/lifecycle/target_tracker.cpp#L95)。

### 73. 查询指定时刻的只读预测（原 K11）

复制当前 filter 到查询时刻，按现行可用性规则返回可空预测，实时状态不前移。

**输入／输出与替换点：**输入当前已确认状态及绝对查询时间；输出 TargetForecast。替换 predict_to 时保留查询不产生新 ID、不改变 step 历史的契约。

**难读点：**当前 tracking_app 的逐帧循环不调用 predict_to；实验或显式调用者使用此旁路，F25 的未来语义方向本次未实施。

定位：[src/tracking/lifecycle/target_tracker.cpp:377](../src/tracking/lifecycle/target_tracker.cpp#L377)。

### 74. 记录跟踪结果和实验误差（原 K12）

输出逐帧状态／事件／预测协议；tracking_lab 另用明确 truth/profile 做标定和 holdout 验证。

**输入／输出与替换点：**输入 TrackingResult 列表与输出路径；输出 tracks.yaml、summary.yaml 或实验 CSV/YAML。替换 writer／lab 指标时保持 schema、来源和失败证据。

**难读点：**现行程序此处主要写 YAML/CSV，原表 JSONL 是概括；四组合通过覆盖合成行为，G-01/G-02/G-03 仍 OPEN。

定位：[src/tracking/io/tracking_io.cpp:303](../src/tracking/io/tracking_io.cpp#L303)。

实际分支：无 live 时直接 create；有 live 才先 predict／associate，未确认时 bootstrap、失配时可能 pending/reseed、确认后才 correct。快照先在 advance 中生成，返回且 input_accepted 后 step 才提交；predict_to 是额外显式只读查询，tracking_app 逐帧主循环不调用它。

## 工具、标定、合成与构建概览

工具不复制整条生产流程：审计显式选 scope，诊断显式选步骤／探针，reader 只重建完整协议，独立 validator 从像素／真值重算。必要 Python 检查在 -O 下仍执行；空统计／单样本未知量不伪装成零。入口见下表和 [工具索引](tools/INDEX.md)。

| 流程 | 输入 → 顺序 → 输出／替换点 | 入口 |
|---|---|---|
| 几何／解码／时序审计 | 配置与明确视频 → 执行范围 → 共用步骤 → 帧证据与运行结论；替换各普通步骤函数，观测不控制算法。 | [tools/audit/decode/decode_audit.cpp:7](../tools/audit/decode/decode_audit.cpp#L7) |
| 协议复算与恢复验证 | JSONL/manifest/原图／冻结参考 → 类型／身份／覆盖 → 独立复算 → PASS/FAIL；reader 与数值 oracle 分开。 | [tools/validation/records/path_a_verify.cpp:989](../tools/validation/records/path_a_verify.cpp#L989) |
| 相机标定 | 严格配置 → VideoInfo → frame_list 样本 → 带 frame_id 的圆点观测 → solver/重投影 → YAML；六个步骤均以显式值交接，未检出保留来源。 | [tools/calibration/apps/calibration_main.cpp:12](../tools/calibration/apps/calibration_main.cpp#L12) |
| 合成夹具 | 冻结输入 → 枚举 → 已知变换 → 扰动 → 栅格 → 真值／编码 → 生成事务 → 独立 verify；各步骤头声明是替换契约，inputs/configs 根与字节保持。 | [tools/synth_ref/main.cpp:43](../tools/synth_ref/main.cpp#L43) |
| 跟踪实验 | 固定实验配置／数据 → calibrate → holdout verify → replay；替换标定／评估需保持现行输出与开放关项。 | [tools/validation/tracking/tracking_lab.cpp:1021](../tools/validation/tracking/tracking_lab.cpp#L1021) |

构建入口是 CMakeLists.txt：只放全局设置与子目录；src 各模块列自己的源和单向链接。types/math/config 在低层，pose 不依赖 detection 实现，tracking 依赖 pose/config/math；observability 只读步骤值，不反向链接检测。tools、tests 分别管理自己的目标，原 mark_* 目标保留必要 ALIAS。behavior 没有实现或占位编译目标。

## 跟一帧调试与阅读取舍

先用既有 fixture 跟单步，再跟完整录像保留前帧历史。后续交互调试可另建 Debug（下面是示例，不冒称本次已执行的 Release 验收）：

```bash
cmake -S src/tushenghao -B /tmp/campus-restructure-debug -DCMAKE_BUILD_TYPE=Debug -DMARK_BUILD_G2=ON -DMARK_BUILD_G4=ON
cmake --build /tmp/campus-restructure-debug -j4
gdb --args /tmp/campus-restructure-debug/marker_app --video /absolute/input.avi --config src/tushenghao/config/detector.yaml --run-dir /tmp/new-debug-run --mode debug --run-purpose verification
```

在 gdb 中 `break mark::Detector::process`，按实际断点号设 `condition 1 frame.frame_id == 784`，然后 run；不要跳过之前帧来解释时序。先看入口是否因现行预算返回 NOT_READY，不为调试填生产预算。沿 runGeometrySteps 的 prepared/components/observations/generated/validated/completed 检查坐标与 ID，再沿 resolver 看绑定／原图弧／线参数／交点／拒绝原因。排序的 double 缓存不能用于 float 后的新输入；独立验证器不读 fitter 缓存。

G2 看 stamp、物理点对、全部候选、eligible_count 与精修前后；G4 在另一进程 tracking_app 的 TargetTracker::step/advance 跟 candidate、input_accepted、events 与提交，再看创新 owner/revision、残差/S/R/score。NIS 是用误差协方差归一化的残差平方；Joseph 更新保持协方差数值性质。时间界不是高斯标准差，合成回放不能变成硬件资格。

记录按 manifest/effective_config → frames.jsonl 的 counts/events/resets/details → export 侧表 → summary/最终覆盖阅读。帧记录外层仍版本 1，角证据子版本 2；缺线模型参数的旧记录明确 UNREPLAYABLE。未执行、关闭计时、null 和未知覆盖均保留区别。gdb 中途退出的目录只作调试材料，不是完整验收证据。

首遍沿 main 与当前连续编号读成功路径，再补拒绝／异常分支；一次只下钻当前步骤及其独立测试。可先跳过周期拟合内部搜索、排序全部能量公式、JSON/YAML 每字段编码和原生库释放细节；输入输出、资源截断、来源身份、独立验证和预算空值不能跳过。历史报告保持其时点含义，当前布局与验证结果以重构记录为准。
