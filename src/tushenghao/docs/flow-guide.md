# 按程序执行顺序阅读瘦身后的代码

从 main 进入，按下面连续编号读到输出；编号是阅读／调用展开顺序，不是新加的运行阶段或类。旧步骤码只作核对。本批主链已由 `marker_app` 在一个进程中连接检测 → G2 位姿 → G4 跟踪；可视化是只读旁支，不回馈主线。完整构建使用 `MARK_BUILD_G2=ON`、`MARK_BUILD_G4=ON`。

板块仍是独立流程步骤。函数、只读值和模块自己的显式状态表达输入、输出和替换点；没有增加新的抽象层。D05–D09 已按 [match-only 规则](MATCH_ONLY_RULES.md) 删除。当前检测只输出物理序四边形，拟合直接使用相邻顶点连线；没有仿射、hypothesis、补片或方向假设归并。M提供P1身份，三个L提供其余角；完整性由当前帧是否存在完整匹配表达。

BGR8 是蓝／绿／红三通道的 8 位图像；RAW 是当前原始检测。K/dist 是内参／畸变参数；profile 是跟踪的数值参数，配置为空时不会自动填入合成数据。物理角身份与屏幕角顺序分别表达。时间界不是高斯标准差；相机系速度含本车运动，是表观速度。

比赛对象是80×80mm的MARK白色结构；115×120×15mm外板不进入PnP尺度或角拟合。V1.0.3与V1.0.2相关图纸和兑换运动规则一致，核对页码见 [本轮报告](MATCH_ONLY_REFACTOR_REPORT.md)。

## 主程序：从配置到一帧，再到运行结束

真实顺序：main → loadConfig → runOffline → 打开输入 → 构造独立模块 → 建输出 → 逐帧 Detector::process → G2 → G4 → 写协议 → 只读渲染 → 写视频 → 下一帧 → 关闭输出与摘要。没有STABLE、对应搜索、窗口或逐帧取证分支。

### 1. 主入口解析运行请求（原 A01）

严格解析 `--video VIDEO` 或 `--camera INDEX` 与可选 `--config YAML`，再调用三参数 runOffline。

**输入／输出与替换点：**输入 argv；输出 AppConfig、输入源、配置路径。替换 parse_options 的调用时保持未知项、重复项和缺值报错。

**难读点：**五个旧开关、窗口和导出旗已删除；现在没有无值开关，视频始终写出。

定位：[tools/apps/marker/main.cpp](../tools/apps/marker/main.cpp)。

### 2. 识别配置协议（原 Q01）

逐层检查 detector YAML 的段、允许键和重复键。

**输入／输出与替换点：**输入配置路径；输出节点与字段约束。G2/G4 分别保留自己的 loader，不由 app 猜测算法参数。

**难读点：**temporal、display、assignment_completion、marker_geometry_path和旧几何验证键不再接受；detector.mode仍为单一SegmentedMark，CLI的--mode已删。

定位：[src/config/detector/config.cpp](../src/config/detector/config.cpp)。

### 3. 转换类型并保留未知值（原 Q02）

各模块显式选择数值节点、完整数字串、可空值等读取规则。

**输入／输出与替换点：**输入 FileNode 与调用者的类型策略；输出强类型配置。替换共享原语时保留各模块原严格程度。

**难读点：**空预算仍未知；删掉无消费者的期待帧数 token 机制，不再保留取证帧范围配置。

定位：[src/config/reader/value_reader.hpp](../src/config/reader/value_reader.hpp)。

### 4. 校验模块参数（原 Q03）

检测、位姿、跟踪分别验证自己的参数；离线输出校验独立。

**输入／输出与替换点：**输入各模块配置；输出合法值或明确错误。可分别替换 validateDetectorConfig、validate_pose_config、validate_tracking_config。

**难读点：**合法配置不表示所有预算已就绪；不填经验值绕过未配置状态。

定位：[src/config/detector/config.cpp](../src/config/detector/config.cpp)。

### 5. 打开输入并读取帧率（原 A04）

runOffline(app, video, config_path) 打开视频或 camera:N，并检查输入帧率。

**输入／输出与替换点：**输入三参数；输出 capture 和 fps。替换输入适配只改此离线边界。

**难读点：**相机仍须由驱动给出有效 fps；没有 GUI、播放节流、subset 或期待总帧数核对。

定位：[src/app/offline/offline_runner.cpp](../src/app/offline/offline_runner.cpp)。

### 6. 构造独立模块并加载资源（原 Q04）

检测器只保存自己的配置，不加载部件模型；开启 G2 时从同目录 pose.yaml 读 calibration.yaml 的 K/dist 与米制点；开启 G4 时读 tracking.yaml 和显式 profile。

**输入／输出与替换点：**输入独立配置与资源路径；输出 Detector、PoseEstimator、可选 TagDetector、TargetTracker。替换一个模块仍消费其公开值契约。

**难读点：**G4 依赖 G2；没有 profile 时跟踪报告 NOT_READY，不能把合成 profile 自动当生产标定。

定位：[src/app/offline/offline_runner.cpp](../src/app/offline/offline_runner.cpp)。

### 7. 建立输出文件

按 output.run_directory 建目录，准备摘要、位姿/跟踪 writer 和视频 exporter。

**输入／输出与替换点：**输入离线输出配置；输出本次文件资源。替换文件输出不改变算法输入。

**难读点：**已有目录拒绝覆盖；没有 manifest、来源指纹、环境快照或逐帧取证记录器。

定位：[src/app/offline/offline_runner.cpp](../src/app/offline/offline_runner.cpp)。

### 8. 逐帧读取并建立时间（原 A04）

录像按 frame_id/fps 建时间轴；相机按主机单调接收时间建时间轴。

**输入／输出与替换点：**输入当前 BGR8 图像；输出 FrameInput 和稍后的 CaptureStamp。替换时间适配必须保留来源和单位。

**难读点：**两者都不伪造曝光时刻或时间误差界；曝光／不确定度留空，接收时刻不能升级为有界时间。

定位：[src/app/offline/offline_runner.cpp](../src/app/offline/offline_runner.cpp)。

### 9. 核验检测输入序列

检查图像类型、帧号和时间顺序；尺寸改变时清输入序列记录。

**输入／输出与替换点：**输入 FrameInput 与实例 last_input；输出 INVALID_INPUT 或进入当前帧检测。替换序列校验保留实例隔离。

**难读点：**Detector 不再拥有平滑、显示历史或诊断会话；reset 只清序列与尺寸。

定位：[src/detection/pipeline/execution/detector.cpp](../src/detection/pipeline/execution/detector.cpp)。

### 10. 预处理并保存坐标映射（原 D02）

preprocess只resize，不修改原图；保存原图只读视图、白色分割阈值和像素中心映射。

**输入／输出与替换点：**输入FrameInput、PreprocessConfig；输出PreparedFrame。替换resize必须保持双向坐标映射。

**难读点：**映射是图像尺寸换算，不是MARK模型变换；后续精角必须来自原图。

定位：[src/detection/preprocess/preprocess.cpp](../src/detection/preprocess/preprocess.cpp)。

### 11. 从工作图提取白片（原 D03）

灰度、白色阈值、外轮廓和原面积下限过滤。WhiteComponent只保留当前轮廓、bbox和碰边标记。

**输入／输出与替换点：**输入PreparedFrame和GeometryConfig；输出白片集合。替换分割不改变后续四边形契约。

**难读点：**没有ShapeObservation、L对应集合或部件ID；这里还没有MARK身份。

定位：[src/detection/geometry/observation/geometry_observation.cpp](../src/detection/geometry/observation/geometry_observation.cpp)。

### 12. 直接匹配 MARK 四边形

按观察白片跨度局部归组，S可作空间连通桥。取实测凸包的四顶点，各由不同角片支撑；六边一凹结构中，外角须与凹点在轮廓上相隔三顶点。按沿相邻框边的归一化跨度区分三个30mm长角与一个14mm短角，分界22/80；这一步只确认三个L＋一个短M，不用M的位置决定P编号。

**输入／输出与替换点：**输入当前白片和原图映射；输出vector<MatchQuad>，每项是原图粗定位凸环，尚无物理身份。可单独替换matchQuads，但必须继续保证实测外边与完整性，物理身份由下一独立步骤查配置映射。

**难读点：**外框是80×80 MARK，绝非115×120外板。无完整匹配就不拟合；不能由三个L补P1。22/80来自图纸，不是像素调参；它不是严格透视不变量，极端视角和混组需重验。

定位：[src/detection/geometry/matching/match_quad.cpp](../src/detection/geometry/matching/match_quad.cpp)。

### 13. 进入独立匹配循环并检查拟合预算

decodeStage先把已完成匹配的四边形交给独立坐标排序函数：按y取上下两点、各按x分左右，得到LT/RT/RB/LB。再查detector.corner.physical_to_screen（P编号→屏幕槽位），按physical[p]=screen[mapping[p]]组成P0…P3后交原拟合器。生产映射[0,1,2,3]，不从短M位置／能量／历史推断身份。匹配、排序、查表与拟合契约分开，均可替换。缺原图观测预算时NOT_READY；每次decode建立一个原图轮廓索引，按需准备，绝不跨帧缓存。

**输入／输出与替换点：**输入PreparedFrame、匹配四边形集合、CornerConfig；输出测量、Detection和原因摘要。各拟合／排序／验证／发布函数仍独立。

**难读点：**同一空间组无法确定唯一MARK时不产生框；已分离的多个MARK分别拟合和发布，没有父假设循环或失败竞争归并。

定位：[src/detection/pipeline/decode/decode_stage.cpp](../src/detection/pipeline/decode/decode_stage.cpp)。

### 14. 四边形给边并取得当前原图轮廓

对q[i]直接使用q[i-1]→q[i]及q[i]→q[i+1]作为两条边提示。OriginalContourIndex每帧只分割一次原图，按粗角邻近边界取完整轮廓；四角不能复用同一原图角片，碰原图边界仍失败。

**输入／输出与替换点：**输入四边形、原图及配置；输出当前角轮廓和两边提示。替换原图取证不能用粗角本身当精角。

**难读点：**没有模型边投影、affine、hypothesis或回调Source；最近轮廓只是来源候选，真正连续边支持仍须通过后面的拟合结构检查。

定位：[src/detection/corners/measurement/corner_resolver.cpp](../src/detection/corners/measurement/corner_resolver.cpp)。

### 15. 提取连续支持弧（原 D12）

用原图轮廓简化顶点和沿弧trim邻域生成候选端点，枚举完整连续弧；同一调用缓存跨度、位置、拟合等重复计算。

**输入／输出与替换点：**输入完整有序轮廓、两条四边形边提示和预算；输出支持弧及关联。替换搜索要保留所有内部像素、连续性和独立支持。

**难读点：**两条路线用同一支持提取；全像素端点路线已删。回走路径不能仅按端点当作相同支持，不能拼不连续白片。

定位：[src/detection/corners/fitting/corner_edge_fit.cpp](../src/detection/corners/fitting/corner_edge_fit.cpp)。

### 16. 两条互补拟合路线（原 D13）

主路普通L2＋原位置关联；没有合法边对才调用周期相位中位数＋后验位置关联。两路共用提取、求交、来源和质量输出。后验关联将实测支持投影到拟合线后检查与提示边的位置关系。

**输入／输出与替换点：**输入同一角的真实连续支持；输出两条物理拟合线、raw残差和模型校正残差。替换估计器保持这些口径分开。

**难读点：**周期模型只解释扫描奇偶偏移，不等于MaixCam必有同样噪声；合法L2不主动改用周期解。ZeroSlope、L1、周期OLS作为独立路线及全端点退路均已删。

定位：[src/detection/corners/fitting/corner_edge_fit.cpp](../src/detection/corners/fitting/corner_edge_fit.cpp)。

### 17. 求交与候选选择（原 D14）

保留独立支持、短连接弧、凸转折、非病态夹角和有限交点检查。可行边对按(r0+r1)×sqrt(1/n0+1/n1)排序，相同分数用弧索引确定性选取；相互冲突交点仍按原64px²预算处理。

**输入／输出与替换点：**输入两条拟合线及完整支持；输出唯一实测交点和证据。原semantic_geometry_threshold在拟合内的用途改名max_pair_disagreement_px2，数值不变。

**难读点：**该分数减轻短弧偶然低残差的偏好，不是协方差或精度证明。原error仍为r0+r1；角误差、extension和残差超限只标质量，不拒绝。

定位：[src/detection/corners/fitting/corner_edge_fit.cpp](../src/detection/corners/fitting/corner_edge_fit.cpp)。

### 18. 形成四角证据并检查必要条件

每角保留物理身份、帧／原图组件ID、连续弧起止、像素、拟合线、交点、raw／校正残差、延伸量和角误差。删除模型边／顶点ID及拼接stable_id字符串。

**输入／输出与替换点：**输入四个成功拟合；输出double CornerMeasurement。生产验证仅检查来源、完整性和数值；独立重拟合与claimed值比对留在测试verifyCornerEvidence。

**难读点：**corner_error是交点到实测连接弧距离，不是独立真值误差；原图证据不完整仍不能伪造成功。

定位：[src/detection/corners/validation/corner_evidence_validation.cpp](../src/detection/corners/validation/corner_evidence_validation.cpp)。

### 19. 排序与原图几何验证（原 D15–D16）

拟合后仍按y分上下、按x分左右形成屏幕序，物理映射直接取安装配置；随后检查四边形有限、严格凸、原图边界与证据来源。

**输入／输出与替换点：**输入四物理角和ScreenOrder；输出验证结果。排序和验证均可单独替换。

**难读点：**屏幕LT/RT/RB/LB不重猜P0…P3；上下带分离的小晃动不改变映射，安装核验步骤见[固定映射报告](FIXED_MAPPING_REPORT.md)；部分出框仍未检出，由G4 coast与重捕接手。

定位：[src/detection/corners/validation/detection_validator.cpp](../src/detection/corners/validation/detection_validator.cpp)。

### 20. 发布 RAW 角点和质量旗

将double实测点转float，再确认屏幕序、映射和bbox，发布Detection。方向来自match固定映射，不再调用语义假设归并。

**输入／输出与替换点：**输入测量及已确定物理身份；输出Detection／FrameResult，供G2读取RAW。替换发布不能裁剪或移动实测角。

**难读点：**quality_flags的1/2/4表示拟合残差／角误差／延伸超配置；只标记，不丢角。

定位：[src/detection/corners/publication/detection_publication.cpp](../src/detection/corners/publication/detection_publication.cpp)。

### 21. 将 RAW 转物理点对并运行 IPPE（原 P01/P05/P06）

按 orientation 还原物理角，与米制模型点配对，调用公共平面 IPPE 求解。

**输入／输出与替换点：**输入 RAW、检测索引、原图尺寸和 CaptureStamp；输出全部候选。替换求解器入口保留物理身份和米制单位。

**难读点：**仍为 SOLVEPNP_IPPE，不改 IPPE_SQUARE；min_quad_area 预算与 LM 精修已删除。

定位：[src/pose/estimation/pose_estimator.cpp](../src/pose/estimation/pose_estimator.cpp)。

### 22. 评价并筛选位姿（原 P07–P09）

独立重投影、正深度、两个重投影预算、安装约束和显式 prior 仍按原顺序筛选。当前参考录像的RMSE／最大单角预算均为1.10px；保留全部IPPE原始候选。

**输入／输出与替换点：**输入全部候选；输出 NoSolution、Ambiguous、BudgetMissing 或 Unique。替换单个谓词仍保留候选及拒绝原因。

**难读点：**两个预算齐全才 budgets_ready；零合格仍NoSolution。有合格支后，若还有两个数值／正深度合法、未被原安装约束或可信prior排除的解，须差支RMSE−好支RMSE≥0.25px才消歧Unique。另一支即使刚超绝对预算也参与差距保护，小差距仍Ambiguous；不按候选槽位强选。没有第二个物理／约束合法解时沿用原筛选。阈值仅在当前录像验证，MaixCam需重验，见[G-01报告](G01_G02_VALIDATION_REPORT.md)。

定位：[src/pose/estimation/pose_estimator.cpp](../src/pose/estimation/pose_estimator.cpp)。

### 23. 发布位姿与质量（原 P11）

充分分离或原独立约束唯一确定的Unique即control_usable；Ambiguous不发布selected_index／位姿。保留stamp、可空协方差、所有原始候选及角点质量旗。

**输入／输出与替换点：**输入筛选结果；输出 PoseObservation。替换 MARK 适配保持当前帧绑定和 corner_quality_flags。

**难读点：**手动 hardware_verified 全删；时间不确定度不在 G2 卡控制资格，G3/G5 应读下游时间标签。

定位：[src/pose/estimation/pose_estimator.cpp](../src/pose/estimation/pose_estimator.cpp)。

### 24. 可选 Tag 分支（原 P02–P04）

配置了白名单才检测 Tag，按原图灰度解码和尺寸构造点对，复用平面求解与筛选。

**输入／输出与替换点：**输入当前图像、stamp、TagConfig；输出 TagObservation 和 ExitObservation 的位姿。替换解码保留后端规范角序、重复 ID 与质量预算。

**难读点：**不把 MARK 屏幕序套给 Tag；未知 ID 尺寸不能猜。Tag 完成后与 MARK 位姿一起交给后续适配。

定位：[src/pose/tag/tag_detector.cpp](../src/pose/tag/tag_detector.cpp)。

### 25. 每帧交付跟踪并检查测量（原 K01–K04）

离线入口直接构造 TrackingFrame；检查当前 Unique、候选证据、身份、位置误差与可选世界变换。

**输入／输出与替换点：**输入本帧位姿与 stamp；输出 PositionMeasurement 集合。替换测量适配保留同帧来源、重复索引与协方差合法性检查。

**难读点：**不再用中间文件串接 pose_app/tracking_app；两入口已删。空检测帧也调用 tracker。

定位：[src/tracking/measurement/measurement_adapter.cpp](../src/tracking/measurement/measurement_adapter.cpp)。

### 26. 计算时间质量（原 K04）

根据绝对界语义、曝光和 profile 中唯一 validated_time_bound_s 判断时间标签。

**输入／输出与替换点：**输入 stamp 与显式 profile；输出本帧时间质量。替换判定不把接收时间或未知误差当真实曝光界。

**难读点：**无 time_policy 模式；缺界不抛构造错误、不清空测量。重叠时间区间降标签，数值仍走名义时间。

定位：[src/tracking/measurement/measurement_adapter.cpp](../src/tracking/measurement/measurement_adapter.cpp)。

### 27. 常速度预测与测量噪声增广（原 K06）

已有轨迹按 dt 预测，再用时间误差及速度二阶矩增广测量 R；未知时间仅作条件数值估计。

**输入／输出与替换点：**输入旧 filter、当前测量、profile 和时间标签；输出预测状态及本帧测量噪声。替换数学函数保留单位、正定和事务回滚。

**难读点：**profile 缺界时以现有 max_dt 尺度增广噪声，不把该代用尺度发布成物理时间界；超 max_dt 的生命周期规则保留。

定位：[src/tracking/lifecycle/target_tracker.cpp](../src/tracking/lifecycle/target_tracker.cpp)。

### 28. 关联、初始化与校正（原 K05/K07/K08）

无 live 且唯一测量时 create；已有轨迹用 NIS 唯一关联，按确认状态 bootstrap 或 correct，失配可 pending/reseed。

**输入／输出与替换点：**输入预测状态、位置测量与 NIS 门；输出本帧测量更新或未更新。替换关联继续使用相同创新 owner/revision。

**难读点：**保留跟踪器自己的测量准入和 NIS；不因为时间标签差就删除合法测量。

定位：[src/tracking/lifecycle/target_tracker.cpp](../src/tracking/lifecycle/target_tracker.cpp)。

### 29. 生命周期、冻结快照与提交（原 K09–K11）

未更新走 coast，超原有失效域则 Lost；先生成快照，input_accepted 后才提交候选状态。

**输入／输出与替换点：**输入候选状态；输出 TargetTrack 和事件。predict_to 仍是显式只读预测，不推进实时历史。

**难读点：**保留位置、表观速度、协方差及合格标签；仅删除 model_hardware_verified。qualified = measured + velocity + position_std + all_time_bounded。

定位：[src/tracking/lifecycle/target_tracker.cpp](../src/tracking/lifecycle/target_tracker.cpp)。

### 30. 写位姿和跟踪协议（原 K12）

完整构建下逐帧写 observations.yaml 和 tracks.yaml；文件输出只消费主线值。

**输入／输出与替换点：**输入 TrackingFrame/TrackingResult；输出 YAML。替换 writer 保留整数、stamp、候选、质量标签和冻结 G4 字段。

**难读点：**录像复跑中 Ambiguous 不产生三维测量；不能为演示跟踪而改预算或强选候选。

定位：[src/tracking/io/tracking_io.cpp](../src/tracking/io/tracking_io.cpp)。

### 31. 叠加当前全链结果（原 A08）

克隆原图，画检测角、MARK 位姿投影、跟踪原点和表观速度；coast 画现有轨迹。

**输入／输出与替换点：**输入只读 RAW、PoseObservation、TargetTrack、K/dist；输出独立 BGR8 overlay。替换渲染不回写任何模块状态。

**难读点：**仅 app 依赖渲染；主线库不链接 app。世界系轨迹缺相机逆变换时不乱投影，仍可显示文字。

定位：[src/app/rendering/debug_renderer.cpp](../src/app/rendering/debug_renderer.cpp)。

### 32. 逐帧写视频（原 A10）

离线工具无条件写 overlay.partial.avi，结束关闭并读回检查后发布 overlay.avi。

**输入／输出与替换点：**输入当前 overlay、源 fps 和视频参数；输出可播放视频文件。替换 exporter 保留失败报错和避免覆盖。

**难读点：**video_fourcc=MJPG、video_filename=overlay.avi；没有窗口、display_probe、export_video 旗或 playback_fps。

定位：[src/app/export/video_exporter.cpp](../src/app/export/video_exporter.cpp)。

### 33. 运行结束并写摘要（原 A12）

关闭输出，按配置记录可选总耗时，写 summary.yaml 并打印检测、位姿和跟踪计数。

**输入／输出与替换点：**输入紧凑 RunSummary；输出摘要和终端说明。替换报告只影响展示。

**难读点：**保留报错和摘要；没有来源证据链、指纹、逐帧取证级别或宣称完整性的期待帧数机制。

定位：[src/app/offline/offline_runner.cpp](../src/app/offline/offline_runner.cpp)。

## 两路线与本轮验证

| 路线 | 支持／位置 | 模型 | 新主链1676帧 | 冻结旧输入4360次 |
|---|---|---|---:|---:|
| 1 | 简化端点邻域／原位置 | 普通L2 | 4352 | 4359 |
| 2 | 同一端点邻域／后验位置 | 周期相位中位数 | 0 | 1 |

旧4360次含frame313和382各一个重复父假设；新主链1088个独立完整匹配×4角=4352次，检出帧集合完全一致。frame784 P1在新边提示下由主路恢复；用户已接受这个触发变化，冻结旧支持与边提示的回归测试仍强制检查回退确实恢复，绝不按帧号选路线。

同一1088帧的最佳正深度PnP候选RMSE中位／P95／最大值为0.382429／0.766330／1.037059px，基线为0.391535／0.797333／1.197445px；这是统计分析，该match-only历史基线a582c48仍1088帧Ambiguous；当前G2筛选交付为1054 Unique／34 Ambiguous／0 NoSolution，同一1088帧的全部候选数值不变，见[G-01报告](G01_G02_VALIDATION_REPORT.md)。合成共同172角的独立真值误差中位不变、P95和最大值降低。真实录像无独立角点真值，不能把PnP改善当成绝对角点精度证明；原像素残差和逐帧差异也在 [完整报告](MATCH_ONLY_REFACTOR_REPORT.md) 中如实列出。

路线统计在tests/detection/corners/corner_route_test.cpp，生产没有全局计数器。冻结frame784反例及连续支持、周期参数、质量标签、自检复算均在自动测试。MaixCam待验项见规则与报告，不能由桌面录像替代。

## 构建、工具与跟一帧调试

构建目标只有vision_*名称；types/math/config在低层，检测／G2／G4独立实现，app消费公开结果并编排。检测不链接位姿或跟踪实现。marker_app是全链离线工具，可视化只读；calibration_tool原样保留，输出K/dist和重投影误差，不自动填写阈值。src/app本分支保持原名。

```bash
cmake -S src/tushenghao -B build -DMARK_BUILD_G2=ON -DMARK_BUILD_G4=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/marker_app --video data/raw/marker_video.avi --config src/tushenghao/config/detector.yaml
./build/corner_route_test data/raw/marker_video.avi /tmp/routes.tsv
# 相机需可用设备和匹配的K/dist、图像域、时间来源：
./build/marker_app --camera 0 --config src/tushenghao/config/detector.yaml
```

输出目录来自detector YAML的output.run_directory，未填为outputs/marker，已有目录拒绝覆盖。G2/G4配置从detector YAML同目录读取pose.yaml/tracking.yaml；全链输出observations.yaml、tracks.yaml、overlay.avi、summary.yaml。G4依赖G2；部分构建只运行启用模块。

调试顺序：Detector::process → runDecodePipeline → matchQuads → resolveObservedCorners → fitObservedEdgePair → IPPE → TargetTracker::step/advance → renderDebugFrame。先看输入域、物理身份和状态，再看支持、残差与协方差。没有检测也要推进G4一帧；G4事务仅input_accepted后提交，predict_to不是当前测量。历史报告和建议书统一在src/tushenghao/docs，当前入口见 [文档索引](VISION_REFACTOR_INDEX.md)。
