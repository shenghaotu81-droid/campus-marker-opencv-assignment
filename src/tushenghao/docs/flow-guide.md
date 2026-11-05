# 按程序执行顺序阅读瘦身后的代码

从 main 进入，按下面连续编号读到输出；编号是阅读／调用展开顺序，不是新加的运行阶段或类。旧步骤码只作核对。本批主链已由 `marker_app` 在一个进程中连接检测 → G2 位姿 → G4 跟踪；可视化是只读旁支，不回馈主线。完整构建使用 `MARK_BUILD_G2=ON`、`MARK_BUILD_G4=ON`。

板块仍是独立流程步骤。函数、只读值和模块自己的显式状态表达输入、输出和替换点；没有增加新的抽象层。D05–D09 按用户后续授权保留，待替代检测规则完成再删。更大的“四边形 + 拟合”结构只在根目录 `CORNER_FIT_REFACTOR_PROPOSAL.md` 提建议，当前尚未实施。

BGR8 是蓝／绿／红三通道的 8 位图像；RAW 是当前原始检测。K/dist 是内参／畸变参数；profile 是跟踪的数值参数，配置为空时不会自动填入合成数据。物理角身份与屏幕角顺序分别表达。时间界不是高斯标准差；相机系速度含本车运动，是表观速度。

## 主程序：从配置到一帧，再到运行结束

真实顺序：main → loadConfig → runOffline → 打开输入 → 构造独立模块 → 建输出 → 逐帧 Detector::process → G2 → G4 → 写协议 → 只读渲染 → 写视频 → 下一帧 → 关闭输出与摘要。没有 STABLE、ExecutionScope、subset、窗口或逐帧取证分支。

### 1. 主入口解析运行请求（原 A01）

严格解析 `--video VIDEO` 或 `--camera INDEX` 与可选 `--config YAML`，再调用三参数 runOffline。

**输入／输出与替换点：**输入 argv；输出 AppConfig、输入源、配置路径。替换 parse_options 的调用时保持未知项、重复项和缺值报错。

**难读点：**五个旧开关、窗口和导出旗已删除；现在没有无值开关，视频始终写出。

定位：[tools/apps/marker/main.cpp:6](../tools/apps/marker/main.cpp#L6)。

### 2. 识别配置协议（原 Q01）

逐层检查 detector YAML 的段、允许键和重复键。

**输入／输出与替换点：**输入配置路径；输出节点与字段约束。G2/G4 分别保留自己的 loader，不由 app 猜测算法参数。

**难读点：**temporal、display、取证 level、mode、expected_frame_count 等旧键不再接受。

定位：[src/config/detector/config.cpp:69](../src/config/detector/config.cpp#L69)。

### 3. 转换类型并保留未知值（原 Q02）

各模块显式选择数值节点、完整数字串、可空值等读取规则。

**输入／输出与替换点：**输入 FileNode 与调用者的类型策略；输出强类型配置。替换共享原语时保留各模块原严格程度。

**难读点：**空预算仍未知；删掉无消费者的期待帧数 token 机制，不再保留取证帧范围配置。

定位：[src/config/reader/value_reader.hpp:7](../src/config/reader/value_reader.hpp#L7)。

### 4. 校验模块参数（原 Q03）

检测、位姿、跟踪分别验证自己的参数；离线输出校验独立。

**输入／输出与替换点：**输入各模块配置；输出合法值或明确错误。可分别替换 validateDetectorConfig、validate_pose_config、validate_tracking_config。

**难读点：**合法配置不表示所有预算已就绪；不填经验值绕过未配置状态。

定位：[src/config/detector/config.cpp:308](../src/config/detector/config.cpp#L308)。

### 5. 打开输入并读取帧率（原 A04）

runOffline(app, video, config_path) 打开视频或 camera:N，并检查输入帧率。

**输入／输出与替换点：**输入三参数；输出 capture 和 fps。替换输入适配只改此离线边界。

**难读点：**相机仍须由驱动给出有效 fps；没有 GUI、播放节流、subset 或期待总帧数核对。

定位：[src/app/offline/offline_runner.cpp:22](../src/app/offline/offline_runner.cpp#L22)。

### 6. 构造独立模块并加载资源（原 Q04）

检测器加载 MarkerGeometry；开启 G2 时从同目录 pose.yaml 读 calibration.yaml 的 K/dist 与米制点；开启 G4 时读 tracking.yaml 和显式 profile。

**输入／输出与替换点：**输入独立配置与资源路径；输出 Detector、PoseEstimator、可选 TagDetector、TargetTracker。替换一个模块仍消费其公开值契约。

**难读点：**G4 依赖 G2；没有 profile 时跟踪报告 NOT_READY，不能把合成 profile 自动当生产标定。

定位：[src/app/offline/offline_runner.cpp:32](../src/app/offline/offline_runner.cpp#L32)。

### 7. 建立输出文件

按 output.run_directory 建目录，准备摘要、位姿/跟踪 writer 和视频 exporter。

**输入／输出与替换点：**输入离线输出配置；输出本次文件资源。替换文件输出不改变算法输入。

**难读点：**已有目录拒绝覆盖；没有 manifest、来源指纹、环境快照或逐帧取证记录器。

定位：[src/app/offline/offline_runner.cpp:45](../src/app/offline/offline_runner.cpp#L45)。

### 8. 逐帧读取并建立时间（原 A04）

录像按 frame_id/fps 建时间轴；相机按主机单调接收时间建时间轴。

**输入／输出与替换点：**输入当前 BGR8 图像；输出 FrameInput 和稍后的 CaptureStamp。替换时间适配必须保留来源和单位。

**难读点：**两者都不伪造曝光时刻或时间误差界；曝光／不确定度留空，接收时刻不能升级为有界时间。

定位：[src/app/offline/offline_runner.cpp:69](../src/app/offline/offline_runner.cpp#L69)。

### 9. 核验检测输入序列

检查图像类型、帧号和时间顺序；尺寸改变时清输入序列记录。

**输入／输出与替换点：**输入 FrameInput 与实例 last_input；输出 INVALID_INPUT 或进入当前帧检测。替换序列校验保留实例隔离。

**难读点：**Detector 不再拥有平滑、显示历史或诊断会话；reset 只清序列与尺寸。

定位：[src/detection/pipeline/execution/detector.cpp:19](../src/detection/pipeline/execution/detector.cpp#L19)。

### 10. 检查预算并预处理（原 D02）

确认 assignment 与原图角观察预算存在，再调用 preprocess 生成工作图和原图映射。

**输入／输出与替换点：**输入当前帧和检测配置；输出 PreparedFrame。替换 preprocess 保留原图只读视图和双向缩放。

**难读点：**删掉 STABLE 预算门后，仅缺实际消费者预算才 NOT_READY；主链只保留一条执行路径。

定位：[src/detection/pipeline/decode/decode_stage.cpp:125](../src/detection/pipeline/decode/decode_stage.cpp#L125)。

### 11. 提取白片并形成形状观察（原 D03–D04）

从当前图像提取带来源 ID 的组件，计算几何与拓扑候选。

**输入／输出与替换点：**输入 PreparedFrame、GeometryConfig；输出 WhiteComponent 和 ShapeObservation。替换观察不提前宣布物理身份。

**难读点：**观察不是 MARK 对应；当前仍由后续 D05–D09 产生定位输入。

定位：[src/detection/geometry/observation/geometry_observation.cpp:2](../src/detection/geometry/observation/geometry_observation.cpp#L2)。

### 12. 枚举三 L 对应（原 D05，本批保留）

枚举观测三 L 和模型三 L 的对应，遵守现有搜索资源上限。

**输入／输出与替换点：**输入只读形状观察与模型；输出 ComponentAssignment 组合。替换枚举必须保留来源身份和截断状态。

**难读点：**删除此块已转后续任务；本批没有把对应藏进拟合或另造替代检测。

定位：[src/detection/geometry/parent/geometry_matcher.cpp:104](../src/detection/geometry/parent/geometry_matcher.cpp#L104)。

### 13. 拟合父仿射假设（原 D06，本批保留）

用模型与实测锚点拟合 model→working 的二维仿射，并装配父假设。

**输入／输出与替换点：**输入候选对应、模型点和观测锚点；输出 GeometryHypothesis。替换拟合不把二维仿射称为相机位姿。

**难读点：**未来四边形输入可能直接给边；那部分只见根目录建议书，本批仍用现有仿射。

定位：[src/detection/geometry/parent/geometry_matcher.cpp:74](../src/detection/geometry/parent/geometry_matcher.cpp#L74)。

### 14. 独立复核父假设（原 D07，本批保留）

按现有轮廓、面积与残差约束复核父候选。

**输入／输出与替换点：**输入父 batch、模型和当前组件；输出通过的 batch 与报错摘要。替换验证不暗改阈值。

**难读点：**本批不精简 G2 screening，也不借瘦身重定检测阈值。

定位：[src/detection/geometry/validation/geometry_validation.cpp:196](../src/detection/geometry/validation/geometry_validation.cpp#L196)。

### 15. 测量缺片候选（原 D08，本批保留）

在父仿射限定的范围测量 M/S 缺片候选。

**输入／输出与替换点：**输入三 L 父候选、组件和显式 completion 预算；输出可供绑定的候选。替换观察保留有限搜索域。

**难读点：**M1 提供 P1；缺少必要对应不能靠屏幕排序或预测角点补上。

定位：[src/detection/geometry/completion/geometry_assignment_completion.cpp:33](../src/detection/geometry/completion/geometry_assignment_completion.cpp#L33)。

### 16. 完成有界绑定（原 D09，本批保留）

完成六片一对一绑定并传播资源截断和完整性。

**输入／输出与替换点：**输入候选与剩余身份；输出 segmented_assignments_ready 的 GeometryBatch。替换绑定保留唯一来源关系。

**难读点：**触发资源上限不能冒称搜索完整；此块等替代检测规则准备好后另删。

定位：[src/detection/geometry/completion/geometry_assignment_completion.cpp:33](../src/detection/geometry/completion/geometry_assignment_completion.cpp#L33)。

### 17. 进入当前帧候选循环

逐候选确认完整片段集合，调用原图四角定位；失败竞争仍计入 unresolved。

**输入／输出与替换点：**输入完成绑定的 batch；输出角测量集合与诊断。替换编排仍显式调用下列独立步骤。

**难读点：**不能丢掉一个无法排除的失败候选，再把剩余候选强称唯一。

定位：[src/detection/pipeline/decode/decode_stage.cpp:16](../src/detection/pipeline/decode/decode_stage.cpp#L16)。

### 18. 按物理身份取原图轮廓和邻边（原 D10–D11）

按 P0…P3 固定绑定查当前白片，关联原图完整轮廓，投影两条模型邻边。

**输入／输出与替换点：**输入 PreparedFrame、组件、hypothesis、模型和 OriginalContourSource；输出每角的轮廓与边提示。替换原图观察不得用模型点替代实测点。

**难读点：**当前入口仍吃 hypothesis；去仿射、去 hypothesis 的四边形架构尚未实施。

定位：[src/detection/corners/measurement/corner_resolver.cpp:13](../src/detection/corners/measurement/corner_resolver.cpp#L13)。

### 19. 枚举连续支持弧（原 D12）

从轮廓端点生成连续弧，按方向、有限边位置、独立支持和实际跨度检查。

**输入／输出与替换点：**输入完整有序轮廓、两条边提示与预算；输出当前尝试的支持集合。替换搜索保留当前帧像素和歧义覆盖。

**难读点：**回走轮廓端点相同不表示支持序列相同，不能用端点去重替代全序列语义。

定位：[src/detection/corners/fitting/corner_edge_fit.cpp:617](../src/detection/corners/fitting/corner_edge_fit.cpp#L617)。

### 20. 按顺序拟合不同模型（原 D13）

先普通 L2，再周期相位／零斜率及必要退路；共 11 个不同尝试，首个成功返回。

**输入／输出与替换点：**输入同一角的轮廓与边；输出两条拟合线及各类残差。替换估计器保持 raw 与模型校正误差分开。

**难读点：**原 8/9 路与 2/4 路完全重复，已删除；零命中不等于所有新场景都可删除该模型。

定位：[src/detection/corners/fitting/corner_edge_fit.cpp:734](../src/detection/corners/fitting/corner_edge_fit.cpp#L734)。

### 21. 求直线交点（原 D14）

检查支持不复用、转折连接、凸性、夹角和有限交点，计算角误差及延伸量。

**输入／输出与替换点：**输入两条真实支持线；输出 PairGeometry 和当前实测交点。替换求交保留数值可求解性。

**难读点：**角误差／extension 超限只保留数值标签；缺支持、近平行和截断仍不能发布成功角。

定位：[src/detection/corners/fitting/corner_edge_fit.cpp:474](../src/detection/corners/fitting/corner_edge_fit.cpp#L474)。

### 22. 形成四角证据并检查必要条件

保留原图来源、残差、模型误差、延伸和角误差；生产检查来源、完整性及数值合法性。

**输入／输出与替换点：**输入 CornerEvidence；输出必要合法性检查结果。独立重拟合、claimed 参数与误差比对由测试侧 verifyCornerEvidence 承担。

**难读点：**生产不再每帧重拟合同一批支持像素；不能把标签值读成角点真值误差或协方差。

定位：[src/detection/corners/validation/corner_evidence_validation.cpp:6](../src/detection/corners/validation/corner_evidence_validation.cpp#L6)。

### 23. 排序与原图几何验证（原 D15–D16）

排序保留物理映射；核对四边形严格凸、有限、在原图内及证据来源。

**输入／输出与替换点：**输入四物理角与 ScreenOrder；输出验证结果。替换排序或验证时保持 double 和 float 的边界。

**难读点：**出框仍未检出；本批不实施部分出框补角。

定位：[src/detection/corners/validation/detection_validator.cpp:270](../src/detection/corners/validation/detection_validator.cpp#L270)。

### 24. 归并语义与方向（原 D17）

比较当前几何解释，按原确定性规则保留测量并判断方向唯一性。

**输入／输出与替换点：**输入四角测量、截断状态和排序结果；输出保留测量与 orientation_unique。替换归并不按最小 PnP 误差猜方向。

**难读点：**物理身份与屏幕角序分别表达；没有方向就留空。

定位：[src/detection/corners/semantics/semantic_resolver.cpp:333](../src/detection/corners/semantics/semantic_resolver.cpp#L333)。

### 25. 发布 RAW 角点和质量旗

double 物理点转 float 后重新确认屏幕序、边界和 bbox，发布当前检测。

**输入／输出与替换点：**输入测量与方向唯一性；输出 Detection，随后 Detector 返回 FrameResult。替换发布不移动原实测物理角。

**难读点：**quality_flags 的 1/2/4 分别表示拟合残差、角误差、支持延伸超配置值；只标记，不删除角点。

定位：[src/detection/corners/publication/detection_publication.cpp:10](../src/detection/corners/publication/detection_publication.cpp#L10)。

### 26. 将 RAW 转物理点对并运行 IPPE（原 P01/P05/P06）

按 orientation 还原物理角，与米制模型点配对，调用公共平面 IPPE 求解。

**输入／输出与替换点：**输入 RAW、检测索引、原图尺寸和 CaptureStamp；输出全部候选。替换求解器入口保留物理身份和米制单位。

**难读点：**仍为 SOLVEPNP_IPPE，不改 IPPE_SQUARE；min_quad_area 预算与 LM 精修已删除。

定位：[src/pose/estimation/pose_estimator.cpp:75](../src/pose/estimation/pose_estimator.cpp#L75)。

### 27. 评价并筛选位姿（原 P07–P09）

独立重投影、正深度、两个重投影预算、安装约束和显式 prior 仍按原顺序筛选。

**输入／输出与替换点：**输入全部候选；输出 NoSolution、Ambiguous、BudgetMissing 或 Unique。替换单个谓词仍保留候选及拒绝原因。

**难读点：**两个已标定预算才决定 budgets_ready；多解不能暗选最小误差。

定位：[src/pose/estimation/pose_estimator.cpp:175](../src/pose/estimation/pose_estimator.cpp#L175)。

### 28. 发布位姿与质量（原 P11）

Unique 即 control_usable；保留 stamp、可空协方差及角点质量旗。

**输入／输出与替换点：**输入筛选结果；输出 PoseObservation。替换 MARK 适配保持当前帧绑定和 corner_quality_flags。

**难读点：**手动 hardware_verified 全删；时间不确定度不在 G2 卡控制资格，G3/G5 应读下游时间标签。

定位：[src/pose/estimation/pose_estimator.cpp:103](../src/pose/estimation/pose_estimator.cpp#L103)。

### 29. 可选 Tag 分支（原 P02–P04）

配置了白名单才检测 Tag，按原图灰度解码和尺寸构造点对，复用平面求解与筛选。

**输入／输出与替换点：**输入当前图像、stamp、TagConfig；输出 TagObservation 和 ExitObservation 的位姿。替换解码保留后端规范角序、重复 ID 与质量预算。

**难读点：**不把 MARK 屏幕序套给 Tag；未知 ID 尺寸不能猜。Tag 完成后与 MARK 位姿一起交给后续适配。

定位：[src/pose/tag/tag_detector.cpp:56](../src/pose/tag/tag_detector.cpp#L56)。

### 30. 每帧交付跟踪并检查测量（原 K01–K04）

离线入口直接构造 TrackingFrame；检查当前 Unique、候选证据、身份、位置误差与可选世界变换。

**输入／输出与替换点：**输入本帧位姿与 stamp；输出 PositionMeasurement 集合。替换测量适配保留同帧来源、重复索引与协方差合法性检查。

**难读点：**不再用中间文件串接 pose_app/tracking_app；两入口已删。空检测帧也调用 tracker。

定位：[src/tracking/measurement/measurement_adapter.cpp:163](../src/tracking/measurement/measurement_adapter.cpp#L163)。

### 31. 计算时间质量（原 K04）

根据绝对界语义、曝光和 profile 中唯一 validated_time_bound_s 判断时间标签。

**输入／输出与替换点：**输入 stamp 与显式 profile；输出本帧时间质量。替换判定不把接收时间或未知误差当真实曝光界。

**难读点：**无 time_policy 模式；缺界不抛构造错误、不清空测量。重叠时间区间降标签，数值仍走名义时间。

定位：[src/tracking/measurement/measurement_adapter.cpp:34](../src/tracking/measurement/measurement_adapter.cpp#L34)。

### 32. 常速度预测与测量噪声增广（原 K06）

已有轨迹按 dt 预测，再用时间误差及速度二阶矩增广测量 R；未知时间仅作条件数值估计。

**输入／输出与替换点：**输入旧 filter、当前测量、profile 和时间标签；输出预测状态及本帧测量噪声。替换数学函数保留单位、正定和事务回滚。

**难读点：**profile 缺界时以现有 max_dt 尺度增广噪声，不把该代用尺度发布成物理时间界；超 max_dt 的生命周期规则保留。

定位：[src/tracking/lifecycle/target_tracker.cpp:179](../src/tracking/lifecycle/target_tracker.cpp#L179)。

### 33. 关联、初始化与校正（原 K05/K07/K08）

无 live 且唯一测量时 create；已有轨迹用 NIS 唯一关联，按确认状态 bootstrap 或 correct，失配可 pending/reseed。

**输入／输出与替换点：**输入预测状态、位置测量与 NIS 门；输出本帧测量更新或未更新。替换关联继续使用相同创新 owner/revision。

**难读点：**保留跟踪器自己的测量准入和 NIS；不因为时间标签差就删除合法测量。

定位：[src/tracking/lifecycle/target_tracker.cpp:273](../src/tracking/lifecycle/target_tracker.cpp#L273)。

### 34. 生命周期、冻结快照与提交（原 K09–K11）

未更新走 coast，超原有失效域则 Lost；先生成快照，input_accepted 后才提交候选状态。

**输入／输出与替换点：**输入候选状态；输出 TargetTrack 和事件。predict_to 仍是显式只读预测，不推进实时历史。

**难读点：**保留位置、表观速度、协方差及合格标签；仅删除 model_hardware_verified。qualified = measured + velocity + position_std + all_time_bounded。

定位：[src/tracking/lifecycle/target_tracker.cpp:93](../src/tracking/lifecycle/target_tracker.cpp#L93)。

### 35. 写位姿和跟踪协议（原 K12）

完整构建下逐帧写 observations.yaml 和 tracks.yaml；文件输出只消费主线值。

**输入／输出与替换点：**输入 TrackingFrame/TrackingResult；输出 YAML。替换 writer 保留整数、stamp、候选、质量标签和冻结 G4 字段。

**难读点：**录像复跑中 Ambiguous 不产生三维测量；不能为演示跟踪而改预算或强选候选。

定位：[src/tracking/io/tracking_io.cpp:267](../src/tracking/io/tracking_io.cpp#L267)。

### 36. 叠加当前全链结果（原 A08）

克隆原图，画检测角、MARK 位姿投影、跟踪原点和表观速度；coast 画现有轨迹。

**输入／输出与替换点：**输入只读 RAW、PoseObservation、TargetTrack、K/dist；输出独立 BGR8 overlay。替换渲染不回写任何模块状态。

**难读点：**仅 app 依赖渲染；主线库不链接 app。世界系轨迹缺相机逆变换时不乱投影，仍可显示文字。

定位：[src/app/rendering/debug_renderer.cpp:7](../src/app/rendering/debug_renderer.cpp#L7)。

### 37. 逐帧写视频（原 A10）

离线工具无条件写 overlay.partial.mp4，结束关闭并读回检查后发布 overlay.mp4。

**输入／输出与替换点：**输入当前 overlay、源 fps 和视频参数；输出可播放视频文件。替换 exporter 保留失败报错和避免覆盖。

**难读点：**video_fourcc/video_filename 保留原值；没有窗口、display_probe、export_video 旗或 playback_fps。

定位：[src/app/export/video_exporter.cpp:23](../src/app/export/video_exporter.cpp#L23)。

### 38. 运行结束并写摘要（原 A12）

关闭输出，按配置记录可选总耗时，写 summary.yaml 并打印检测、位姿和跟踪计数。

**输入／输出与替换点：**输入紧凑 RunSummary；输出摘要和终端说明。替换报告只影响展示。

**难读点：**保留报错和摘要；没有来源证据链、指纹、逐帧取证级别或宣称完整性的期待帧数机制。

定位：[src/app/offline/offline_runner.cpp:146](../src/app/offline/offline_runner.cpp#L146)。

## 拟合路线与本批复跑

11 个不同尝试的顺序如下。每项仍调用同一个独立 stage，不复制拟合实现；只去掉重复调度和只供旧实验入口使用的策略开关。

| 当前编号 | 原编号 | 支持／位置方式 | 拟合模型 | 1676 帧命中 |
|---|---|---|---|---:|
| 1 | 1 | 原端点／原位置 | Legacy L2 | 4359 |
| 2 | 2（8 为重复） | 原端点／原位置 | PhaseMedian | 0 |
| 3 | 3 | 原端点／原位置 | ZeroSlope | 0 |
| 4 | 4（9 为重复） | 原端点／后验位置 | PhaseMedian | 1 |
| 5 | 5 | 原端点／后验位置 | ZeroSlope | 0 |
| 6 | 6 | 原端点／后验位置 | Legacy L2 | 0 |
| 7 | 7 | 全端点／原位置 | Legacy L2 | 0 |
| 8 | 10 | 全端点／原位置 | PhaseMedian | 0 |
| 9–11 | 11–13 | 原端点、后验、全端点 | Periodic L1 | 0 |

命中统计在 `tests/detection/corners/corner_route_test.cpp`，生产没有全局计数器。4360 次候选角拟合在合并前后逐项一致，最大坐标差 0px；1088 个检出帧的合法正深度候选最小 RMSE 中位均为 0.3915354321px。该最小值只用于统计，实际 1088 帧均保持 Ambiguous；没有强选为 Unique。它与历史约 0.43px 的口径不完全相同，不能当作真实角点精度改善。完整结果、局限和复跑位置见根目录 `SLIMMING_REPORT.md`。

## 构建、工具与跟一帧调试

构建目标仅保留一套 `vision_*` 名称；没有 `vision::x` / `mark_x` 双别名或 `*_internal_headers` 转发层。types/math/config 在低层；检测、位姿、跟踪分别拥有实现；app 编排并消费它们，摘要与渲染不反向控制算法。`src/app` 本批不改名。

`marker_app` 是全链入口；`pose_app`、`tracking_app`、audit、validation、fixture 与 synth_ref 工具已删。calibration_tool 在本分支原样保留，搬仓时再取走；标定输出 K、畸变和重投影误差，不自动填写阈值。其余历史 docs 保持原样，本导读是本批唯一更新的保留文档。

```bash
cmake -S src/tushenghao -B build -DMARK_BUILD_G2=ON -DMARK_BUILD_G4=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/marker_app --video data/raw/marker_video.avi --config src/tushenghao/config/detector.yaml
# 相机输入；需要可用设备与匹配的相机标定：
./build/marker_app --camera 0 --config src/tushenghao/config/detector.yaml
```

输出目录来自 detector YAML 的 `output.run_directory`，未填时为当前目录下 `outputs/marker`，已有目录拒绝覆盖。G2/G4 配置从 detector YAML 同目录的 pose.yaml/tracking.yaml 读取；更新相机时相应 calibration.yaml 必须匹配。全链输出是 observations.yaml、tracks.yaml、overlay.mp4、summary.yaml；不完整构建只运行已开启模块。

调试沿 Detector::process → runDecodePipeline → resolver → fitter → IPPE → TargetTracker::step/advance → renderDebugFrame 跟一帧。G4 的事务候选只有 input_accepted 后提交；没有检测也必须推进一帧。查询 predict_to 不等于当前测量。首遍先看输入输出和状态，再下钻支持弧、周期模型和协方差数学；生产不再通过取证 recorder 读取内部状态，完整像素复算见测试侧 verifyCornerEvidence。
