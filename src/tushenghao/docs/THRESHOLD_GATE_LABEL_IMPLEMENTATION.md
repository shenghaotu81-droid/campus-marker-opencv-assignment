# 156项阈值、门控、标签落实表

日期：2026-11-06；分支 `refactor/vision-lean-fixed`；基线 `f1ee955`，审查/工作树快照 `60c4a6f`。

本表逐项对应[原审查](THRESHOLD_GATE_LABEL_AUDIT.md)。最新用户要求的“单候选四边形引导＋删除测试专属生产支撑”覆盖原表中依赖多候选的保留建议；因此旧D11/H15/H16等不机械搬进新实现。P03与G01在同一实现中改语义。没有另造无人消费的标签。

原表R/C/F/T/U证据等级仍有效；下面的“保留”不宣称每个保护都在实拍拒过坏数据：空图、NaN、越界、退化、错帧等仍主要是坏输入测试证据。录像588帧未检出没有逐门归因。新量测、消融、冻结反例和限制见[修改报告](SINGLE_CANDIDATE_SLIMMING_REPORT.md)，不是给每条结构判定编造实拍触发。

定位指向本次代码；已删项指其现在的替代/收敛位置，原位置可从快照审查链接查看。

| ID | 原项目 | 落实结果 | 当前定位 |
|---|---|---|---|
| D01 | preprocess.work_width=960、work_height=720 | 保留工作图尺寸：坐标换算/resize契约；非法尺寸才拒。 | [CFG:18](../include/vision/config/detector_config.hpp#L18) [DC:42](../src/config/detector/config.cpp#L42) |
| D02 | preprocess.threshold=200 | 保留原图白阈200：产生真实支持，属于分割本体。 | [CO:29](../src/detection/corners/observation/corner_observation.cpp#L29) [DC:42](../src/config/detector/config.cpp#L42) |
| D03 | geometry.white_threshold=200 | 保留工作图白阈200：候选分割本体。 | [GO:5](../src/detection/geometry/observation/geometry_observation.cpp#L5) [DC:42](../src/config/detector/config.cpp#L42) |
| D04 | geometry.approximation_epsilon=2.0 | 保留工作图epsilon=2：轮廓构形参数，非质量预算。 | [MQ:56](../src/detection/geometry/matching/match_quad.cpp#L56) [CT:20](../src/detection/geometry/observation/contour_topology.hpp#L20) |
| D05 | geometry.min_area=0.0 | 删除min_area、面积计算和恒假门。 | [GO:5](../src/detection/geometry/observation/geometry_observation.cpp#L5) [CFG:18](../include/vision/config/detector_config.hpp#L18) |
| D06 | corner.max_pair_disagreement_px2=64（8px差） | 删除64px²冲突预算和候选冲突逻辑；单候选没有路线冲突可标。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| D07 | corner.min_line_points=10 | 删除10点门及重复自证；L2只需两点、非零跨度。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| D08 | corner.max_line_fit_error=0.5px | 删除0.5px残差阈值、质量bit及透传；无消费者。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) [DT:15](../include/vision/types/detector_types.hpp#L15) |
| D09 | corner.min_intersection_angle_deg=5° | 删除5°质量门；保留机器epsilon平行保护与有限交点。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| D10 | corner.max_corner_error=4.131595359474918px | 删除4.1316px角误差阈值、bit及专属距离计算。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) [DP:4](../src/detection/corners/publication/detection_publication.cpp#L4) |
| D11 | corner.approximation_epsilon=1.0px | 旧原图RDP epsilon=1随候选搜索删除；改为原轮廓连续步行，非删真实支持。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| D12 | observation.max_edge_direction_diff_deg=3° | 删除3°方向门；两组按邻边方向最近归属，不设差值资格。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| D13 | observation.max_edge_position_distance_px=9px | 删除9px位置关联门；以最近原轮廓锚点及连续外边代替。 | [CO:29](../src/detection/corners/observation/corner_observation.cpp#L29) [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| D14 | observation.turn_trim_distance_px=2px | 保留2px转折修剪为局部支持构造常量；去掉配置和旧双重用途。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| D15 | observation.max_turn_connection_length_px=13.5px | 删除13.5px连接预算及三处剪枝；连续性由双向不重叠步行保证。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| D16 | observation.max_support_extension_px=9.5px | 删除9.5px extension阈值、bit及专属计算。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) [DP:4](../src/detection/corners/publication/detection_publication.cpp#L4) |
| D17 | observation.min_support_span_px=14px | 删除14px支持跨度门；只保留真实两点且不重合。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| D18 | physical_to_screen=[0,1,2,3] | 保留显式P→屏幕排列；加载/直接构造验一次，G2独立接口仍验下标。 | [DC:42](../src/config/detector/config.cpp#L42) [DS:9](../src/detection/pipeline/decode/decode_stage.cpp#L9) [PE:53](../src/pose/estimation/pose_estimator.cpp#L53) |
| D19 | timing_enabled=1；MJPG／overlay.avi | 删除timing开关，固定统计耗时；保留MJPG/AVI完整性。 | [APP:24](../src/app/offline/offline_runner.cpp#L24) [DC:42](../src/config/detector/config.cpp#L42) |
| P01 | budgets.max_rmse_px=1.10px | 删除max_rmse质量门；保留真实RMSE，选择名义支并给G4连续权重。 | [PG:53](../src/pose/validation/pose_geometry.cpp#L53) [PE:53](../src/pose/estimation/pose_estimator.cpp#L53) [MA:26](../src/tracking/measurement/measurement_adapter.cpp#L26) |
| P02 | budgets.max_corner_error_px=1.10px | 删除max_corner_error门、字段及重复最大误差计算。 | [PG:53](../src/pose/validation/pose_geometry.cpp#L53) [PT:40](../include/vision/types/pose_types.hpp#L40) |
| P03 | selection.min_rmse_gap_px=0.25px | 删除0.25px差距门；两合法解仍Ambiguous，最佳支可测量；G01同步放行并消费分歧。 | [PE:53](../src/pose/estimation/pose_estimator.cpp#L53) [MA:26](../src/tracking/measurement/measurement_adapter.cpp#L26) |
| P04 | min_z_m="" | 删除关闭的安装/prior配置、数据类型、校验与估计路径（原P04）。 | [PC:75](../src/config/pose/pose_config.cpp#L75) [PE:53](../src/pose/estimation/pose_estimator.cpp#L53) |
| P05 | max_z_m="" | 删除关闭的安装/prior配置、数据类型、校验与估计路径（原P05）。 | [PC:75](../src/config/pose/pose_config.cpp#L75) [PE:53](../src/pose/estimation/pose_estimator.cpp#L53) |
| P06 | min_normal_z="" | 删除关闭的安装/prior配置、数据类型、校验与估计路径（原P06）。 | [PC:75](../src/config/pose/pose_config.cpp#L75) [PE:53](../src/pose/estimation/pose_estimator.cpp#L53) |
| P07 | max_normal_z="" | 删除关闭的安装/prior配置、数据类型、校验与估计路径（原P07）。 | [PC:75](../src/config/pose/pose_config.cpp#L75) [PE:53](../src/pose/estimation/pose_estimator.cpp#L53) |
| P08 | prior_enabled=0 | 删除关闭的安装/prior配置、数据类型、校验与估计路径（原P08）。 | [PC:75](../src/config/pose/pose_config.cpp#L75) [PE:53](../src/pose/estimation/pose_estimator.cpp#L53) |
| P09 | prior_max_age_s="" | 删除关闭的安装/prior配置、数据类型、校验与估计路径（原P09）。 | [PC:75](../src/config/pose/pose_config.cpp#L75) [PE:53](../src/pose/estimation/pose_estimator.cpp#L53) |
| P10 | prior_max_translation_m="" | 删除关闭的安装/prior配置、数据类型、校验与估计路径（原P10）。 | [PC:75](../src/config/pose/pose_config.cpp#L75) [PE:53](../src/pose/estimation/pose_estimator.cpp#L53) |
| P11 | prior_max_rotation_rad="" | 删除关闭的安装/prior配置、数据类型、校验与估计路径（原P11）。 | [PC:75](../src/config/pose/pose_config.cpp#L75) [PE:53](../src/pose/estimation/pose_estimator.cpp#L53) |
| P12 | prior可信、身份、帧号、曝光/时钟和有限性复合检查 | 删除关闭的安装/prior配置、数据类型、校验与估计路径（原P12）。 | [PC:75](../src/config/pose/pose_config.cpp#L75) [PE:53](../src/pose/estimation/pose_estimator.cpp#L53) |
| P13 | camera/model/unit/target_id及schema=1 | 保留相机矩阵、畸变、图像域、模型尺度/单位等实际算子前提。 | [PC:75](../src/config/pose/pose_config.cpp#L75) [PE:53](../src/pose/estimation/pose_estimator.cpp#L53) |
| A01 | family=tag36h11；allowed_ids=[]；sizes=[] | 保留退出Tag ID白名单及实际边长；未知边长仍能像素解码，米制PnP无解。 | [TAG:32](../src/pose/tag/tag_detector.cpp#L32) [PC:75](../src/config/pose/pose_config.cpp#L75) |
| A02 | nthreads=1 | 删除线程数配置；后端固定1，避免板端嵌套线程。 | [TAG:32](../src/pose/tag/tag_detector.cpp#L32) |
| A03 | quad_decimate=1.0 | 删除decimate配置；后端固定1，不先丢分辨率。 | [TAG:32](../src/pose/tag/tag_detector.cpp#L32) |
| A04 | quad_sigma=0.0 | 删除sigma配置；后端固定0，不额外模糊。 | [TAG:32](../src/pose/tag/tag_detector.cpp#L32) |
| A05 | refine_edges=1 | 删除refine_edges配置；后端固定启用。 | [TAG:32](../src/pose/tag/tag_detector.cpp#L32) |
| A06 | decode_sharpening=0.25 | 删除sharpening配置；维持后端原0.25设置。 | [TAG:32](../src/pose/tag/tag_detector.cpp#L32) |
| A07 | max_hamming=0（加载允许0..2） | 删除外层max_hamming配置及重复门；后端按0纠错位解码。 | [TAG:32](../src/pose/tag/tag_detector.cpp#L32) |
| A08 | min_decision_margin="" | 删除decision_margin质量门、配置及死字段。 | [TAG:32](../src/pose/tag/tag_detector.cpp#L32) |
| A09 | ID重复、非法四边形、空C句柄、stride>INT_MAX | 保留真实解码输入、ID/重复ID/角点合法性；去掉重复布尔。 | [TAG:32](../src/pose/tag/tag_detector.cpp#L32) [PE:53](../src/pose/estimation/pose_estimator.cpp#L53) |
| K01 | profile="" | 空profile仍可运行检测/G2；启动说明一次，不造每帧ready标签。 | [APP:24](../src/app/offline/offline_runner.cpp#L24) [TC:30](../src/config/tracking/tracking_config.cpp#L30) |
| K02 | source=MARK、target_class固定、coordinate_mode=camera_relative、world_frame_id=local-world | 删除未接线世界系/变换/coordinate_mode抽象；只保留相机系。 | [TC:30](../src/config/tracking/tracking_config.cpp#L30) [TR:40](../src/tracking/lifecycle/target_tracker.cpp#L40) |
| K03 | stamp_uncertainty_semantics=unknown | 保留实际时间事实与absolute_bound语义；未知走名义模型，不猜界。 | [TC:30](../src/config/tracking/tracking_config.cpp#L30) [TR:40](../src/tracking/lifecycle/target_tracker.cpp#L40) [MA:26](../src/tracking/measurement/measurement_adapter.cpp#L26) |
| K04 | association_nis_limit=12.5649982152（外置） | 删除association NIS阈值；NIS只排序多个观测并诊断。 | [TR:40](../src/tracking/lifecycle/target_tracker.cpp#L40) |
| K05 | bootstrap_nis_limit=6.15063781638（外置） | 删除bootstrap NIS阈值；第二个不同时刻建立速度。 | [TR:40](../src/tracking/lifecycle/target_tracker.cpp#L40) [KF:41](../src/tracking/filter/kalman_cv.cpp#L41) |
| K06 | position_std_limit_m=0.00388040270251（外置） | 删除位置std门及其派生std/eigen输出。 | [TR:40](../src/tracking/lifecycle/target_tracker.cpp#L40) [TT:15](../include/vision/types/tracking_types.hpp#L15) |
| K07 | velocity_std_limit_mps=0.0570262589346（外置） | 删除速度std门及其派生输出；P本体保留。 | [TR:40](../src/tracking/lifecycle/target_tracker.cpp#L40) [TT:15](../include/vision/types/tracking_types.hpp#L15) |
| K08 | max_dt_s=0.0176263179（外置） | 删除max_dt/预测资格门；任意正有限dt正常预测、coast。 | [TR:40](../src/tracking/lifecycle/target_tracker.cpp#L40) [KF:41](../src/tracking/filter/kalman_cv.cpp#L41) |
| K09 | validated_time_bound_s=0.000137575337824（外置） | 删除validated_time_bound和未知时间补猜；只消费实际声明的界。 | [TC:30](../src/config/tracking/tracking_config.cpp#L30) [TR:40](../src/tracking/lifecycle/target_tracker.cpp#L40) |
| K10 | camera_mse_m2=原profile 3×3矩阵 | 保留R正定测量噪声；再以两支连续分歧增广。 | [TC:30](../src/config/tracking/tracking_config.cpp#L30) [MA:26](../src/tracking/measurement/measurement_adapter.cpp#L26) |
| K11 | acceleration_psd_m2ps3=0.00826459324569（外置） | 保留过程噪声q，允许数学有效的q=0。 | [TC:30](../src/config/tracking/tracking_config.cpp#L30) [KF:41](../src/tracking/filter/kalman_cv.cpp#L41) |
| K12 | initial_velocity_variance_m2ps2=0.130944780307（外置） | 保留初速方差vv>0：初始化可观测性需要。 | [TC:30](../src/config/tracking/tracking_config.cpp#L30) [KF:41](../src/tracking/filter/kalman_cv.cpp#L41) |
| K13 | profile camera_id/coordinate_mode匹配 | 删除profile相机错配逐帧警告/门；G2实际相机绑定仍保留。 | [TC:30](../src/config/tracking/tracking_config.cpp#L30) [PC:75](../src/config/pose/pose_config.cpp#L75) |
| K14 | profile.evidence_id/basis非空 | 删除evidence/hardware/basis等profile强制元数据。仅R/q/vv参与数值。 | [TC:30](../src/config/tracking/tracking_config.cpp#L30) |
| K15 | profile.time_policy/hardware_verified旧键 | 删除旧时间政策/资格字段与验证分支；不伪造硬件验证。 | [TC:30](../src/config/tracking/tracking_config.cpp#L30) [TR:40](../src/tracking/lifecycle/target_tracker.cpp#L40) |
| H01 | 局部分组距离hypot(dx,dy) <= max白片bbox跨度 | 保留按白片跨度局部归组，防不同目标与亮背景混框。 | [MQ:56](../src/detection/geometry/matching/match_quad.cpp#L56) |
| H02 | 6尺度{1,.5,.75,1.25,1.5,2}，拓扑/凸包各一套 | 量贡献后保留六尺度：缩成四尺度会使小尺寸/抖动测试漏检，不能仅因本片零命中删除。 | [MQ:56](../src/detection/geometry/matching/match_quad.cpp#L56) |
| H03 | simple顶点数target..target+2且<=8；mask选target个 | 简化至6点或7→6单顶点修复；删除8点mask组合。无修复实拍少1帧。 | [CT:20](../src/detection/geometry/observation/contour_topology.hpp#L20) |
| H04 | 合并边覆盖完整轮廓弧的segmentDistance<=epsilon | 保留全原弧到合并边epsilon约束：结构真实性，非拟合质量门。 | [CT:20](../src/detection/geometry/observation/contour_topology.hpp#L20) |
| H05 | 六边、恰一凹点、无平直转向，外角=凹点+3(mod6) | 保留六边一凹、真实外凸角：L/M结构本体。 | [MQ:56](../src/detection/geometry/matching/match_quad.cpp#L56) [CT:20](../src/detection/geometry/observation/contour_topology.hpp#L20) |
| H06 | 凸包原始点>=3；粗框4点且凸；正绕序 | 保留四点凸包/绕序，不能凭标签补造缺角。 | [MQ:56](../src/detection/geometry/matching/match_quad.cpp#L56) [SO:6](../src/detection/corners/ordering/screen_order.cpp#L6) |
| H07 | 四顶点owner各不相同；最近原始角片归属 | 保留四个不同角片及最近归属，不新增距离门。 | [MQ:56](../src/detection/geometry/matching/match_quad.cpp#L56) |
| H08 | touches_border（工作图bbox碰边） | 保留工作图截断检查；原图另核真实支持，两图表示不同并非同输入重复自证。 | [GO:5](../src/detection/geometry/observation/geometry_observation.cpp#L5) [MQ:56](../src/detection/geometry/matching/match_quad.cpp#L56) [CO:29](../src/detection/corners/observation/corner_observation.cpp#L29) |
| H09 | 短角span<22/80，且short_count==1 | 保留三L一短M、22/80结构分界；不用于定P身份。严重透视待板端重验。 | [MQ:56](../src/detection/geometry/matching/match_quad.cpp#L56) |
| H10 | nearest原图轮廓；原轮廓size>=3；任一点碰原图边即拒 | 保留最近真实原轮廓、轮廓前提及原图截断；不裁ROI造闭合边。 | [CO:29](../src/detection/corners/observation/corner_observation.cpp#L29) |
| H11 | 边指导finite且端点不重合；polygon.size>=3 | 保留有限粗边/非零方向和真实轮廓；没有对象就无解。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| H12 | 弧trim后为空、不连续则不用；不同弧不能共享像素 | 保留连续、互不重用的两组支持，直接由双向半轮廓步行构造。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| H13 | 两条弧须对应两条不同粗边；connector沿原轮廓正向；叉积×winding>0 | 保留两组不同邻边归属；匹配器已定外凸角，无旧弧对/connector枚举。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) [MQ:56](../src/detection/geometry/matching/match_quad.cpp#L56) |
| H14 | L2 fitLine DIST_L2，reps/aeps=0.01/0.01；方向norm>0 | 保留各一次L2 fitLine和两线求交；0.01为原数值算子参数。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| H15 | score=(r0+r1)*sqrt(1/n0+1/n1)，相同按弧ID字典序 | 旧评分及字典序随多候选删除；新方案没有候选可以评分。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| H16 | L2失败才进入周期相位中位数＋后验关联 | 评估后删除周期回退；实拍4352/4352单L2成功，冻结784旧反例也恢复。六个旧指导无支持例另披露。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| H17 | 周期support>=3、两奇偶相位都存在 | 周期辨识性检查随周期模型删除，不改成新主路标签。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| H18 | determinant>64ε·max(1,tt·hh)，相位坐标需可llround | 周期秩/相位取整保护随模型删除；主路自己的平行/有限保护保留。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| H19 | 轮廓长度前缀误差64ε·count·max(1,total) | 删除服务于13.5连接门的前缀和/舍入剪枝。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| H20 | 缓存已prepared、支持alias去重、AABB不相交跳共享像素计算 | 删除弧alias/AABB交集缓存；仅留原图轮廓每帧按需提取一次。 | [CO:29](../src/detection/corners/observation/corner_observation.cpp#L29) [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| V01 | 入口BGR8、2D、非空、stamp非负/已知枚举 | Detector公开入口一次BGR8/2D/时间检查；删除恒为Unknown的TimestampSource及枚举自证，decode不重复。 | [DE:21](../src/detection/pipeline/execution/detector.cpp#L21) [DS:9](../src/detection/pipeline/decode/decode_stage.cpp#L9) |
| V02 | frame_id严格递增；时间不倒退；尺寸变化reset | 保留公开帧序/时间契约和尺寸切换；reset不再带无人消费的Reason枚举。 | [DE:21](../src/detection/pipeline/execution/detector.cpp#L21) |
| V03 | observation_budget存在且非负有限/角度<=90/跨度>0/min点>=3等 | 删全部observation预算及六层就绪检查；配置仅在加载/公开构造边界验。 | [DC:42](../src/config/detector/config.cpp#L42) [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| V04 | 工作/原图类型、精确scale比、索引frame_id相同 | 索引生命周期绑定PreparedFrame，删内部图型/scale/frame_id自证副本；不同模块公开契约仍独立。 | [CO:29](../src/detection/corners/observation/corner_observation.cpp#L29) [CR:7](../src/detection/corners/measurement/corner_resolver.cpp#L7) [DS:9](../src/detection/pipeline/decode/decode_stage.cpp#L9) |
| V05 | 固定映射必须为0..3排列，反复排序验证 | 加载/Detector直接构造验排列，G2独立入口验排列；删除内部重复used数组。 | [DC:42](../src/config/detector/config.cpp#L42) [PE:53](../src/pose/estimation/pose_estimator.cpp#L53) |
| V06 | 屏幕排序finite、点距<1e-9、同向叉积、／2area／<2e-9 | 只留finite+真正非零叉积+同向凸环；删1e-9/面积近零及重复点距。 | [SO:6](../src/detection/corners/ordering/screen_order.cpp#L6) |
| V07 | 粗环排序检查后resolver又查；精角排序后validator再验；float后再验 | 粗环一次排序；double→float改变表示后再验一次，删中间反复自检。 | [SO:6](../src/detection/corners/ordering/screen_order.cpp#L6) [DP:4](../src/detection/corners/publication/detection_publication.cpp#L4) |
| V08 | 验证器非零边＋严格凸＋非邻边自交三遍 | 删除validator及非零边/自交/面积三套重复；一个严格凸环足够。 | [SO:6](../src/detection/corners/ordering/screen_order.cpp#L6) |
| V09 | 精角及每个支持/连接像素都须在原图内 | 删除对findContours像素逐个重复在图内自证；保留最终float角坐标界。 | [CO:29](../src/detection/corners/observation/corner_observation.cpp#L29) [DP:4](../src/detection/corners/publication/detection_publication.cpp#L4) |
| V10 | 重建points必须精确等于order.screen_points | 删除屏幕/物理映射正反互证；直接查已验证排列。 | [DS:9](../src/detection/pipeline/decode/decode_stage.cpp#L9) [DP:4](../src/detection/corners/publication/detection_publication.cpp#L4) |
| V11 | 4角不复用组件、同frame、e.physical_corner==index | 保留resolver四个真实组件不重复，删Evidence逐角ID的再验证。 | [CR:7](../src/detection/corners/measurement/corner_resolver.cpp#L7) |
| V12 | e.truncated/original_observation/component_id/segment ID合法 | 删除Evidence源像素集合复制与逐层回查；支持只在本地拟合。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) [CR:7](../src/detection/corners/measurement/corner_resolver.cpp#L7) |
| V13 | e.error/corner_error/各种mean/max/extension非负有限 | 删除Evidence残差/线模型回算；输出就是本次拟合交点。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| V14 | line方向非零、支持size>=min_points、turn_arc>=2 | 删除Evidence线/弧/质量值自检与budget再验证，测试直接验证公开结果。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| V15 | float舍入后非有限、重复、越界、凸性、size>0 | 保留float发布后有限/图界/严格凸环，防表示变化。 | [DP:4](../src/detection/corners/publication/detection_publication.cpp#L4) [SO:6](../src/detection/corners/ordering/screen_order.cpp#L6) |
| V16 | G2输入frame/time/检测索引/存在orientation/排列 | 保留G2独立公开输入的帧对应、detection下标与映射合法性。 | [PE:53](../src/pose/estimation/pose_estimator.cpp#L53) |
| V17 | G2 stamp/相机id/尺寸/RAW域；模型有限z=0、四点严格凸 | 保留公开时间/图像域/四角数学契约；stamp验证由共享方法实现。 | [PT:40](../include/vision/types/pose_types.hpp#L40) [PE:53](../src/pose/estimation/pose_estimator.cpp#L53) |
| V18 | G2 K结构、dist长度4/5/8/12/14、模型平行四边形残差<=1e-12 | 保留一次相机/模型配置验证，克隆K/dist避免外部别名修改。 | [PC:75](../src/config/pose/pose_config.cpp#L75) [PE:53](../src/pose/estimation/pose_estimator.cpp#L53) |
| V19 | PnP候选非有限/重投影非有限/所有模型点min_depth>0 | 保留IPPE有限位姿、正深度、可投影/RMSE有限；不套经验质量门。 | [PG:53](../src/pose/validation/pose_geometry.cpp#L53) |
| V20 | 无候选/全候选被拒/预算缺失/多解/solver异常 | 只有没有数学合法解才NoSolution；双解仍Ambiguous但可提供名义测量。 | [PE:53](../src/pose/estimation/pose_estimator.cpp#L53) [PT:40](../include/vision/types/pose_types.hpp#L40) |
| G01 | source/target与同帧绑定；Unique＋T＋selected_index存在/不越界 | 删除Unique-only准入；Unique/Ambiguous都可提供合法名义测量，分歧进R。 | [MA:26](../src/tracking/measurement/measurement_adapter.cpp#L26) |
| G02 | 恰一eligible、选中eligible/metrics_valid、candidate.reasons必须空 | 删除eligible/reasons.empty等第二套资格；只看实际有效候选。 | [PT:40](../include/vision/types/pose_types.hpp#L40) [MA:26](../src/tracking/measurement/measurement_adapter.cpp#L26) |
| G03 | 候选/公开T finite、深度>0、误差非负有限 | 保留G4所消费t/RMSE有限和正深度；RMSE现用于连续权重，不是质量阈值。 | [MA:26](../src/tracking/measurement/measurement_adapter.cpp#L26) |
| G04 | 同detection_index不能出现多个Unique；noise绑定最多1个 | 保留同帧重复detection_index排除，防重复更新同一观测。 | [MA:26](../src/tracking/measurement/measurement_adapter.cpp#L26) |
| G05 | PositionNoise evidence_id非空、R正定；world变换R/t/协方差合法 | 删除PositionNoise/world/provenance通用层；R来自所选profile并在构造验证。 | [TC:30](../src/config/tracking/tracking_config.cpp#L30) [MA:26](../src/tracking/measurement/measurement_adapter.cpp#L26) |
| G06 | frame stamp非空/非负、same_context否则reset、帧序/采样时间严格递增 | 保留真实时序、上下文切换、同帧绑定，防错帧更新。 | [TR:40](../src/tracking/lifecycle/target_tracker.cpp#L40) [MA:26](../src/tracking/measurement/measurement_adapter.cpp#L26) |
| G07 | 两帧时间绝对界之和>=dt只降current_time_bounded | 保留可用的实际时间界增广；时间区间重叠回名义模型，不拒测量。 | [TR:40](../src/tracking/lifecycle/target_tracker.cpp#L40) [KF:41](../src/tracking/filter/kalman_cv.cpp#L41) |
| G08 | measurements.size()==1才create，多条不选；多个NIS通过则coast | 无轨迹多观测或NIS精确平局不硬认身份；正常单目标无质量门。 | [TR:40](../src/tracking/lifecycle/target_tracker.cpp#L40) |
| G09 | NIS全拒且仅一个测量→pending→两点bootstrap/reseed，ID不复用 | 删除高NIS拒绝、pending、重种及其状态机；任意有限创新都更新。 | [TR:40](../src/tracking/lifecycle/target_tracker.cpp#L40) |
| G10 | confirmed=false不发布速度 | 保留内部两时刻建速度；初帧不伪称测得零速。 | [TR:40](../src/tracking/lifecycle/target_tracker.cpp#L40) [KF:41](../src/tracking/filter/kalman_cv.cpp#L41) |
| G11 | 预测position/velocity std超限→available=false；无confirmed、请求过去时也不给 | 删除无人消费的predict_to及输出时域预算。 | [TT:15](../include/vision/types/tracking_types.hpp#L15) [TR:40](../src/tracking/lifecycle/target_tracker.cpp#L40) |
| G12 | LocalWorld变换、sqrt(eps)正交/行列式/对称性、无ego协方差标签 | 删除未用世界系变换/坐标配置及相关检查。 | [TT:15](../include/vision/types/tracking_types.hpp#L15) [TC:30](../src/config/tracking/tracking_config.cpp#L30) |
| G13 | b>=0、q>=0、bootstrap dt>0且bound<dt；predict dt>=0 | 保留dt/噪声/量纲/有限数等算子定义域。 | [KF:41](../src/tracking/filter/kalman_cv.cpp#L41) |
| G14 | R/P有限且正定、矩阵维度/类型、舍入对称eps·n·scale | 保留可变P/R的正定及矩阵形状/有限验证。 | [KF:41](../src/tracking/filter/kalman_cv.cpp#L41) [TC:30](../src/config/tracking/tracking_config.cpp#L30) |
| G15 | Cholesky失败、NIS<0/非有限、状态溢出、std特征值<0 | 保留求解失败/溢出保护，不靠质量门替代数学失败。 | [KF:41](../src/tracking/filter/kalman_cv.cpp#L41) [TR:40](../src/tracking/lifecycle/target_tracker.cpp#L40) |
| G16 | Innovation owner==this且revision匹配 | 删除只支撑测试误用的innovation owner/revision；创新是本步内部临时值。 | [KF:41](../src/tracking/filter/kalman_cv.cpp#L41) [TR:40](../src/tracking/lifecycle/target_tracker.cpp#L40) |
| G17 | step事务input_accepted才提交；异常不污染state | 保留一步候选状态提交；异常回滚不污染后续帧。 | [TR:40](../src/tracking/lifecycle/target_tracker.cpp#L40) |
| L01 | LineFitResidualHigh / bit1 | 删除角质量bit及拷贝/序列化/空值检查（原L01）；没有决策消费者。 | [DT:15](../include/vision/types/detector_types.hpp#L15) [PT:40](../include/vision/types/pose_types.hpp#L40) [PI:14](../src/pose/io/pose_io.cpp#L14) |
| L02 | CornerErrorHigh / bit2 | 删除角质量bit及拷贝/序列化/空值检查（原L02）；没有决策消费者。 | [DT:15](../include/vision/types/detector_types.hpp#L15) [PT:40](../include/vision/types/pose_types.hpp#L40) [PI:14](../src/pose/io/pose_io.cpp#L14) |
| L03 | SupportExtensionHigh / bit4 | 删除角质量bit及拷贝/序列化/空值检查（原L03）；没有决策消费者。 | [DT:15](../include/vision/types/detector_types.hpp#L15) [PT:40](../include/vision/types/pose_types.hpp#L40) [PI:14](../src/pose/io/pose_io.cpp#L14) |
| L04 | Detection.quality_flags→PoseObservation.corner_quality_flags | 删除角质量bit及拷贝/序列化/空值检查（原L04）；没有决策消费者。 | [DT:15](../include/vision/types/detector_types.hpp#L15) [PT:40](../include/vision/types/pose_types.hpp#L40) [PI:14](../src/pose/io/pose_io.cpp#L14) |
| L05 | CornerEvidence.error_（两边raw mean之和） | 旧残差评分随多候选一并删除；新拟合没有候选评分需求。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| L06 | line_mean_residual_px_/line_max_residual_px_ | 删除重型CornerEvidence残差/支持元数据；交点仍是结果。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) [CR:7](../src/detection/corners/measurement/corner_resolver.cpp#L7) |
| L07 | original_observation_=true、truncated_=false | 删除单一模型恒定字段和自证；函数输入本就确定模型。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| L08 | corner_error_px_、support_extension_px_ | 删除无人消费的corner_error/support_extension副本与专属计算。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) [CR:7](../src/detection/corners/measurement/corner_resolver.cpp#L7) |
| L09 | model_mean_residual_px_/model_max_residual_px_ | 删除模型残差均值/最大值透传；不保留只供自证的重复指标。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| L10 | original_support_arcs_完整像素向量 | 删除完整原始支持弧输出/拷贝；本地L2支持仍保留。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| L11 | original_turn_arc_完整连接弧 | 删除完整连接弧输出与专属计算；连续性直接在支持步行构造中保证。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| L12 | observed_segment_ids_/end_ids_ | 删除弧候选索引vector/排序记录；单候选不需要。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| L13 | edge_segment_a_/b_、line_a_/b_、intersection_ | 删除Evidence端点/线输出副本；局部两条线和最终交点保留。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| L14 | line_model_kinds_/periodic_axes_/periodic_coefficients_ | 删除周期参数诊断及模型类型枚举；周期模型已删。 | [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) |
| L15 | physical_corner_/frame_id_/component_id_逐角复制 | 删除逐角P/frame副本；P是数组下标，真实组件去重仅在resolver。 | [CR:7](../src/detection/corners/measurement/corner_resolver.cpp#L7) |
| L16 | FrameResult/DecodeStageResult.diagnostics与每层reason串 | 删除逐候选字符串计数；保留公开失败边界短原因，不据文本作质量总门。 | [DE:21](../src/detection/pipeline/execution/detector.cpp#L21) [CF:7](../src/detection/corners/fitting/corner_edge_fit.cpp#L7) [PT:40](../include/vision/types/pose_types.hpp#L40) |
| L17 | MarkCategory::Unknown、confidence空、marker_code空、bbox | 删除category/confidence/code/bbox与attributes壳；orientation是G2实际输入仍留。 | [DT:15](../include/vision/types/detector_types.hpp#L15) |
| L18 | ScreenOrderStatus＋optional；CornerResolutionStatus＋optional | 删除重复Status+optional层；角点结果用optional，边界留短reason。 | [CR:7](../src/detection/corners/measurement/corner_resolver.cpp#L7) [SO:6](../src/detection/corners/ordering/screen_order.cpp#L6) |
| L19 | DecodeStageResult.measurements | 删除decode.measurements及整帧Evidence复制。 | [DS:9](../src/detection/pipeline/decode/decode_stage.cpp#L9) |
| L20 | PoseCandidate.metrics_valid/eligible | 删除metrics_valid/eligible：只存本地数学检验通过的候选。 | [PG:53](../src/pose/validation/pose_geometry.cpp#L53) [PT:40](../include/vision/types/pose_types.hpp#L40) |
| L21 | candidate.reasons与observation.reasons | 删除候选/reasons数组及非空即拒；只留公共数学失败原因。 | [PT:40](../include/vision/types/pose_types.hpp#L40) [PE:53](../src/pose/estimation/pose_estimator.cpp#L53) |
| L22 | control_usable_ | 删除control_usable重复布尔，不把名义测量宣传为控制资格。 | [PT:40](../include/vision/types/pose_types.hpp#L40) |
| L23 | selected_rmse_px_ | 删除selected_rmse副本，通过measurement取候选。 | [PT:40](../include/vision/types/pose_types.hpp#L40) |
| L24 | covariance_se3_空占位 | 删除从不生产的SE3协方差占位。 | [PT:40](../include/vision/types/pose_types.hpp#L40) |
| L25 | Tag.whitelisted_/decode_accepted_与Exit.alignment_measurement_usable_ | 删除Tag资格双布尔、Exit wrapper/alignment空输出；直接像素观察/PoseObservation。 | [TAG:32](../src/pose/tag/tag_detector.cpp#L32) [PT:40](../include/vision/types/pose_types.hpp#L40) |
| L26 | Track.confirmed_ | 删除公开confirmed；内部两时刻可观测状态仍用于速度初始化。 | [TR:40](../src/tracking/lifecycle/target_tracker.cpp#L40) [TT:15](../include/vision/types/tracking_types.hpp#L15) |
| L27 | current/all_time_bounded内部与TargetTrack.time_bounded输出 | 删除公开time_bounded和长期汇总；真实stamp时间界仍在公式中消费。 | [TR:40](../src/tracking/lifecycle/target_tracker.cpp#L40) [TT:15](../include/vision/types/tracking_types.hpp#L15) |
| L28 | qualified_for_g5_ | 删除无消费者的qualified_for_g5及其专属组合判断。 | [TT:15](../include/vision/types/tracking_types.hpp#L15) [TR:40](../src/tracking/lifecycle/target_tracker.cpp#L40) |
| L29 | uncertainty_scope_、velocity_meaning_重复长字符串 | 删除每轨迹长语义串；相机系/表观速度含义统一写入接口文档。 | [TT:15](../include/vision/types/tracking_types.hpp#L15) |
| L30 | observation_age_s_、input_time_bound_s_、last_measurement_stamp | 删除可导出age/重复界；保留原始input/last_measurement/state_time。 | [TT:15](../include/vision/types/tracking_types.hpp#L15) |
| L31 | Measured/Coasting/Lost、position/velocity/P与track_id | 保留ID、有限位置/速度/P、Measured/Coasting；删只由质量预算制造的Lost清空。 | [TT:15](../include/vision/types/tracking_types.hpp#L15) [TR:40](../src/tracking/lifecycle/target_tracker.cpp#L40) |
| L32 | input_accepted_、ready_、association_nis_、events_ | 删除ready/每帧常量警告；保留提交信号、实际异常事件和关联NIS。 | [TR:40](../src/tracking/lifecycle/target_tracker.cpp#L40) [TI:5](../src/tracking/io/tracking_io.cpp#L5) |
| Q01 | YAML必填/类型/数字完整解析/finite/布尔0或1/schema | 保留加载schema/类型/finite/完整数字，删僵尸字段校验。配置schema统一2。 | [DC:42](../src/config/detector/config.cpp#L42) [PC:75](../src/config/pose/pose_config.cpp#L75) [TC:30](../src/config/tracking/tracking_config.cpp#L30) [VR:8](../src/config/reader/value_reader.hpp#L8) |
| Q02 | detector未知/重复键；输入BGR8/us；SegmentedMark唯一枚举 | 删除唯一mode/input格式通用分派；保留未知/重复键错误及公开BGR/us契约。 | [DC:42](../src/config/detector/config.cpp#L42) [DE:21](../src/detection/pipeline/execution/detector.cpp#L21) |
| Q03 | 标签预算必须有限非负；角度(0,90]；min_points>=3；epsilon>0等 | 删所有已删预算的加载/验证/写出/roundtrip，只有实际参数还验定义域。 | [DC:42](../src/config/detector/config.cpp#L42) [PC:75](../src/config/pose/pose_config.cpp#L75) [TC:30](../src/config/tracking/tracking_config.cpp#L30) |
| Q04 | G2 min/max成对/区间、prior三预算、Tag ID0..586/side>0 | 删除未用安装/prior成对/范围验证；保留真实Tag ID/已知side合法性。 | [PC:75](../src/config/pose/pose_config.cpp#L75) [TAG:32](../src/pose/tag/tag_detector.cpp#L32) |
| Q05 | profile要求所有7个数值有限且>0 | profile只需R/q/vv，q允许0；加载和直接公开构造各验自己输入，不新增validated包装层。 | [TC:30](../src/config/tracking/tracking_config.cpp#L30) [KF:41](../src/tracking/filter/kalman_cv.cpp#L41) |
| Q06 | G2资源路径/ G4 profile路径不允许absolute | 删除absolute路径禁令；允许配置相对与显式绝对路径，文件失败仍报错。 | [PC:75](../src/config/pose/pose_config.cpp#L75) [TC:30](../src/config/tracking/tracking_config.cpp#L30) |
| Q07 | G4读回版本1/4、单位m/us、整数/枚举、矩阵shape/finite、空数据集拒绝 | 生产无reader消费者：删除旧/现行bulk reader与writer；最小schema5 reader在测试回放工具。 | [PI:14](../src/pose/io/pose_io.cpp#L14) [TI:5](../src/tracking/io/tracking_io.cpp#L5) |
| Q08 | 几何clamp[0,1]、acos clamp[-1,1]；无方向/无长度返回无效量 | 仅保留实际使用的segmentDistance/clamp；删bounds/boundaryDistance/angle等死几何函数。 | [CT:20](../src/detection/geometry/observation/contour_topology.hpp#L20) |
| Q09 | Kalman dt³/3、dt²/2；未知相关MSE的2倍；Joseph对称化0.5 | 保留CV积分、Joseph、对称化和实际有界公式；未知时间不凭空套2倍包络。 | [KF:41](../src/tracking/filter/kalman_cv.cpp#L41) [MA:26](../src/tracking/measurement/measurement_adapter.cpp#L26) |
| Q10 | 渲染z>0、世界系无逆变换不投影、100ms速度箭头 | 保留正深度/有限投影与100ms箭头；删世界系、bounded/qualified显示。 | [RE:8](../src/app/rendering/debug_renderer.cpp#L8) |
| Q11 | 视频/文件打开、fps>0、帧周期/尺寸/写后全帧读回 | 保留文件/视频/FPS/写出读回完整性。MJPG AVI本轮仍完整解码1676帧。 | [APP:24](../src/app/offline/offline_runner.cpp#L24) |

## 本次额外清理

- `fit trace`：随单候选重写完全删除；实拍复跑走公开Detector/G2/G4，不增加生产探针。
- `writeEffectiveConfig`：函数、声明、字段写出表及依赖它的round-trip测试删除。配置测试直接读手写输入，覆盖有效值/错字段/坏值，不用生产写出器造期望输入。
- 模型多边形加载器与相关模型类型只被测试使用，已移除；测试渲染自己的图纸fixture，生产仍读取PnP需要的四点模型。
- 删除只供旧测试/离线复算的Evidence校验器、参考拟合实现、生产G4读回/批量写出/预测接口、测试用innovation owner/revision；当前序列化只保留应用实际调用的逐帧writer。
- `tests/support`、长录像回放工具仅进入BUILD_TESTING构建；不进生产库。真正的标定工具写出功能有实际工具消费者，保留。
- 确认`detector_debug.yaml`、`detector_verification.yaml`与`detector.yaml`字节相同且无源码/构建/测试引用后删除；扫描config目录无其他重复内容组。旧报告/证据中的同名路径保留历史含义。
