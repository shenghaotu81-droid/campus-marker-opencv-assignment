# 视觉管线最终精简第二轮扫查报告

> 此页为第二轮 `fa6d394` 的裁决来源。源码位置对应当时版本，执行结果见[第三轮修改说明](FINAL_LEAN_CHANGELOG_3.md)。

## 同类问题待裁决

“当前调用路径”表示条件已被现有上游覆盖。将函数作为独立入口时，需重新判断输入契约。历史证据中的实现按其冻结版本核对。

| 位置 | 发现 | 对应原则 |
|---|---|---|
| [prepared_frame_types.hpp](../include/vision/types/prepared_frame_types.hpp#L55)、[preprocess.cpp](../src/detection/preprocess/preprocess.cpp#L16) | PreparedFrame 的帧号和时间戳只写不读。Detector 给 FrameResult 写的是原 FrameInput 字段。 | 冗余字段。 |
| [prepared_frame_types.hpp](../include/vision/types/prepared_frame_types.hpp#L44) | original_to_work 没有现行调用。几何匹配只使用工作图到原图的换算。 | 未使用的接口。 |
| [detector.cpp](../src/detection/pipeline/detector.cpp#L86)、[decode_stage.cpp](../src/detection/pipeline/decode_stage.cpp#L93) | run_decode_pipeline 只返回 NOT_DETECTED 和 DETECTED。Detector 在解码后检查 INVALID_INPUT 的 reset 分支不可达。 | 检查须有触发路径。 |
| [detector.cpp](../src/detection/pipeline/detector.cpp#L66) | 初次设置结果帧身份只服务于非法输入提前返回。合法路径上，解码覆盖整个对象，再重写元数据。显式 INVALID_INPUT 也重复类型默认值。 | 重复初始化。 |
| [detector.cpp](../src/detection/pipeline/detector.cpp#L26) | validate_frame_stamp 只有一个调用点。成功分支清空本次调用中新建的空 reason，且调用方只在失败时读原因。 | 单使用者抽象和冗余写入。 |
| [decode_stage.cpp](../src/detection/pipeline/decode_stage.cpp#L4)、[detector.cpp](../src/detection/pipeline/detector.cpp#L8)、[preprocess.cpp](../src/detection/preprocess/preprocess.cpp#L2)、[geometry_observation.cpp](../src/detection/geometry/geometry_observation.cpp#L3)、[corner_observation.cpp](../src/detection/corners/corner_observation.cpp#L3) | 模块路径与同目录或相对路径两套 include 指向同一批头，分别有 5、1、1、1、1 组重复。 | 重复补丁没有独立用途。 |
| [contour_topology.hpp](../src/detection/geometry/contour_topology.hpp#L67)、[match_quad.cpp](../src/detection/geometry/match_quad.cpp#L25) | 唯一调用将原轮廓和从该轮廓取出的简化顶点一起传入。查找顶点下标有用途，但“未找到就返回”的分支缺少当前调用路径的触发场景。 | 区分计算与防御。 |
| [pose_estimator.cpp](../src/pose/estimation/pose_estimator.cpp#L82)、[pose_config.cpp](../src/config/pose/pose_config.cpp#L34) | MARK 的 z 由配置加载器写死为 0。非平面条件只能来自程序化配置或其他公开调用。可评估将通用求解输入检查与手填模型的重复约束分开。 | 手填 YAML 与算法输入的防护边界。 |
| [tag_detector.cpp](../src/pose/tag/tag_detector.cpp#L92) | 空的本地 Mat 经 BGR8→灰度后，单通道单字节，行步长由 int 宽度生成。没有外部 stride 注入，未找到 TAG_STRIDE_OVERFLOW 的实际触发输入。 | 防御分支须有触发场景。 |
| [tag_detector.cpp](../src/pose/tag/tag_detector.cpp#L161)、[pose_estimator.cpp](../src/pose/estimation/pose_estimator.cpp#L76) | 正常 Tag 路径先验四边形，平面求解又验同一四边形。独立像素输出和通用求解各有输入边界，串联时检查重复。 | 重复检查候选，须先明确入口契约。 |
| [tag_detector.cpp](../src/pose/tag/tag_detector.cpp#L49) | 本机 AprilTag 头文件写明 quad_decimate=1 时忽略 refine_edges，而注释称精修会生效。该参数组已冻结，若继续裁决需先验证部署库的实际行为。 | 缺少当前生效场景的机制。 |
| [target_tracker.cpp](../src/tracking/lifecycle/target_tracker.cpp#L40) | live 一旦建轨即不再清空。每实例 next_id++ 只执行一次，产生 ID 恒为 1。供外部关联的字段可与内部递增机制分别裁决。 | 无第二次触发的计数机制。 |
| [target_tracker.cpp](../src/tracking/lifecycle/target_tracker.cpp#L168) | 确认后仍写入 seed，但后续只走 correct，初速 bootstrap 不再读取它。 | 无消费者的写入。 |
| [kalman_cv.cpp](../src/tracking/filter/kalman_cv.cpp#L76) | position_block 只给 innovation 用一次，无独立声明或测试入口。 | 单使用者抽象。 |
| [kalman_cv.cpp](../src/tracking/filter/kalman_cv.cpp#L192) | predict 的唯一生产调用从已接受的、非负且严格递增的 int64 微秒时间求差。该路径不能生成非有限或负 dt。独立内部调用是否需要兜底待裁决。 | 检查须有真实触发路径。 |
| [target_tracker.cpp](../src/tracking/lifecycle/target_tracker.cpp#L137) | 一个候选 R 非正定或求新息失败会使整帧回滚，同帧合法候选也不能参与选择。需要裁决整帧事务与坏候选隔离的边界。 | 门控范围是否应限制在坏数据。 |
| [calibration_video.hpp](../tools/calibration/frames/calibration_video.hpp#L12)、[calibration_result.hpp](../tools/calibration/solution/calibration_result.hpp#L14) | 标定总帧数无消费者。源路径转抄到结果后，与结果帧 ID 列表一样均未进入摘要、YAML 或现行测试。原始抽帧编号仍用于失败日志。 | 未消费元数据。 |
| [calibration_result.cpp](../tools/calibration/solution/calibration_result.cpp#L54)、[calibration_detection.cpp](../tools/calibration/observation/calibration_detection.cpp#L52) | CLI 在完全无检出时已退出。两个点数组始终同步 push，空集谓词等价。独立求解入口是否保留一个空集检查可单独裁决。 | 上游覆盖和重复谓词。 |
| [calibration_video.cpp](../tools/calibration/frames/calibration_video.cpp#L31) | 空帧表提前返回 true 与零次循环后返回 true 效果相同。 | 冗余分支。 |
| [calibration_detection.cpp](../tools/calibration/observation/calibration_detection.cpp#L6)、[calibration_result.cpp](../tools/calibration/solution/calibration_result.cpp#L7) | 标定网格生成、独立 RMSE 各为单调用点 helper。独立复核仍有用途，抽象层与复核是否保留可分别裁决。 | 单使用者抽象。 |
| [tools/CMakeLists.txt](../tools/CMakeLists.txt#L1)、[tools/calibration/CMakeLists.txt](../tools/calibration/CMakeLists.txt#L2) | 仅启用测试、关闭工具时仍构建 vision_calibration。现行 11 个测试未链接，唯一链接者是已关闭的 calibration_tool。 | 无消费者的构建工作。 |
| [block3_fixture.hpp](../tests/support/detection/block3_fixture.hpp#L86) | fixture::resolve 无调用者，配置实参也只转为 void。边拟合测试直接调用生产函数。 | 死代码与无用参数。 |
| [support_pixel_count.hpp](../tests/support/detection/corners/support_pixel_count.hpp#L17) | 无现行 include 或调用，支持点统计只见于历史证据。 | 未进入现行构建的抽象。 |
| [tracking_replay.cpp](../tests/validation/tracking_replay.cpp#L7)、[tracking_replay.cpp](../tests/validation/tracking_replay.cpp#L32) | uint64 帧号经 int64 中转，超 INT64_MAX 的十进制字符串无法完整回读。观测旋转、RMSE 和观测子帧戳也只为回放 reader 自用，跟踪不消费。 | 读写边界不一致，回放冗余字段需明确契约。 |
| [offline_chain_test.cpp](../tests/app/offline/offline_chain_test.cpp#L82)、[pose_contract_test.cpp](../tests/pose/estimation/pose_contract_test.cpp#L21) | 离线链仍检查旧资格键缺席。空 MARK 用例仍设置已无消费者的帧戳匹配和检测状态字段。 | 是否继续保留历史防回归断言和夹具字段待裁决。 |

## 注释修改整理建议

### 接口、配置和检测

| 位置 | 问题与建议 |
|---|---|
| [tracking_types.hpp](../include/vision/types/tracking_types.hpp#L43) | input_accepted 仍列已删上下文检查。建议记录负时间、帧序和成功推进，说明提交由 step 决定。 |
| [tracking_types.hpp](../include/vision/types/tracking_types.hpp#L7) | Vec/Matx 不保证在栈上，Mat 可借外部存储。建议按定长内联存储、Mat 头和底层缓冲所有权解释。 |
| [detector_types.hpp](../include/vision/types/detector_types.hpp#L32) | FrameResult 未继承 FrameInput。建议写独立携带输入帧号和时间。 |
| [corner_types.hpp](../include/vision/types/corner_types.hpp#L9) | 混用物理角与屏幕顺序。建议把类型描述为四个像素坐标，顺序由具体接口声明。 |
| [pose_types.hpp](../include/vision/types/pose_types.hpp#L15) | 板端到达时刻写成当前来源，现行只用离线名义时间。建议把板端语义放移植约定。 |
| [pose_config.hpp](../include/vision/config/pose_config.hpp#L12) | K/dist 的解释分散且把常见五系数形状写成类型保证。建议只留标定配置契约、单位与尺寸绑定，投影推导放导读。 |
| [detector_config.hpp](../include/vision/config/detector_config.hpp#L9)、[preprocess.cpp](../src/detection/preprocess/preprocess.cpp#L1)、[corner_observation.cpp](../src/detection/corners/corner_observation.cpp#L27) | 960×720、白阈 200 多处被写成固定值。建议区分 YAML 当前值、默认值与各阶段用途。 |
| [prepared_frame_types.hpp](../include/vision/types/prepared_frame_types.hpp#L32) | 半像素说明混用索引与像素左上角，帧上下文说明也声称用于解码关联。建议直接写整数像素中心的双向换算，去掉无消费者字段的假定用途。 |
| [detector.hpp](../include/vision/detection/detector.hpp#L31)、[detector.cpp](../src/detection/pipeline/detector.cpp#L83) | impl 由初始化列表构造，注释误称构造函数体赋值。解码处多处复述覆写元数据。建议分别简述对象职责和“公开入口补帧身份”。 |
| [decode_stage.cpp](../src/detection/pipeline/decode_stage.cpp#L83) | 发布 Point2f 行尾将类型转换说成 float→double。建议说明前置 double→float 舍入、升回 double 校验，最后把已舍入值写回 Point2f。 |
| [decode_stage.cpp](../src/detection/pipeline/decode_stage.cpp#L1)、[preprocess.cpp](../src/detection/preprocess/preprocess.cpp#L5)、[geometry_observation.cpp](../src/detection/geometry/geometry_observation.cpp#L1)、[corner_observation.cpp](../src/detection/corners/corner_observation.cpp#L1) | 重复文件头、编辑器找路径的临时说明与库签名遮住阶段职责。建议各留一处输入、输出或轮廓策略说明。 |
| [geometry_observation.cpp](../src/detection/geometry/geometry_observation.cpp#L41)、[prepared_frame_types.hpp](../include/vision/types/prepared_frame_types.hpp#L15) | “最小正矩形”易误会成正方形。建议统一为轴对齐外接矩形，注明右、下开边界。 |
| [contour_topology.hpp](../src/detection/geometry/contour_topology.hpp#L33)、[match_quad.cpp](../src/detection/geometry/match_quad.cpp#L36) | 叉积乘有向面积用于统一符号，却称为转角量值归一化，并直接套用 y 向上的顺逆时针说法。建议写同号为凸、异号为凹并区分坐标约定。 |
| [match_quad.cpp](../src/detection/geometry/match_quad.cpp#L14) | 四行重复六边规则，“相隔三个顶点”易误读。建议直接用 outer == (notch + 3) % 6 表达相对下标。 |
| [corner_observation.cpp](../src/detection/corners/corner_observation.cpp#L33) | “点越密越准，压缩就没法用”泛化。建议说明本实现依赖连续支持和固定索引切线窗。 |
| [corner_observation.cpp](../src/detection/corners/corner_observation.cpp#L69) | 触边被说成一定截断。建议说明这是基于截断风险的不完整支持判定。 |
| [corner_edge_fit.cpp](../src/detection/corners/corner_edge_fit.cpp#L61) | 半窗 3 是索引步数，起步与末端裁剪按累计弧长像素。建议区分单位并删掉“像素噪声尺度固定”的断言。 |
| [corner_edge_fit.cpp](../src/detection/corners/corner_edge_fit.cpp#L80) | 分组相同不必然说明两侧实际是同一条边。建议写两探针落入同一指导方向组，当前证据无法区分。 |
| [corner_edge_fit.cpp](../src/detection/corners/corner_edge_fit.cpp#L124)、[corner_edge_fit.cpp](../src/detection/corners/corner_edge_fit.cpp#L140) | 把 reps/aeps 写成本次 L2 的停止条件，把小叉积写成交点已到无穷远。建议记录一次 L2 调用和分母保护，若写迭代细节则按部署的 OpenCV 实现核实。 |

### 位姿、跟踪和序列化

| 位置 | 问题与建议 |
|---|---|
| [pose_estimator.hpp](../include/vision/pose/pose_estimator.hpp#L23)、[pose_estimator.cpp](../src/pose/estimation/pose_estimator.cpp#L95) | 有些行称 IPPE 一定拿到两个候选，通用求解内又把外参都说成 MARK 的。建议统一为至多两个，按实际合法集合处理。MARK 和 Tag 坐标约定各归包装入口。 |
| [pose_estimator.cpp](../src/pose/estimation/pose_estimator.cpp#L152)、[covariance_validation.cpp](../src/tracking/filter/covariance_validation.cpp#L62) | Mat::at 说明让人误以为始终有边界检查。建议写实际类型、形状、索引，注明断言受构建模式影响。 |
| [pose_estimator.cpp](../src/pose/estimation/pose_estimator.cpp#L199) | 备用似然公式、sigma 试验和未来五选一留在 S 循环旁，易被误读为当前路径。建议把备选方案移到实验记录，代码旁保留实际逆平方权重、零误差特例和二阶矩。 |
| [pose_estimator.cpp](../src/pose/estimation/pose_estimator.cpp#L241) | 把 S 直接称为已知协方差，声称真值必为两个解之一、零协方差等于独立。建议只作名义解周围加权位置二阶矩解释，概率解释注明所需假设。 |
| [pose_geometry.cpp](../src/pose/validation/pose_geometry.cpp#L86) | 累计 min_depth 可能略过作为 std::min 第二参数的 NaN。建议注明这条检查的对象是累计最小值，重投影另外检查有限性。 |
| [tag_detector.cpp](../src/pose/tag/tag_detector.cpp#L74)、[tag_detector.cpp](../src/pose/tag/tag_detector.cpp#L168) | “所有 tag”遗漏白名单、几何和同 ID 整组排除。remove_if 不保证尾部是原淘汰元素。建议描述过滤后的返回集合和保留元素前移。 |
| [pose_io.cpp](../src/pose/io/pose_io.cpp#L22) | source 说明只列 MARK、APRILTAG，通用接口还会生成 PLANAR。建议注明它列的是离线取值，或补通用来源。 |
| [pose_io.cpp](../src/pose/io/pose_io.cpp#L29) | if 的条件声明被说成带初始化语句。建议写条件中声明 optional 引用，再按是否含值分支。 |
| [kalman_cv.cpp](../src/tracking/filter/kalman_cv.cpp#L87)、[kalman_cv.hpp](../src/tracking/filter/kalman_cv.hpp#L1) | “真正的速度”“第二帧”和单向生命周期遗漏缺测与预测顺序。建议写下一次成功关联的区间差分估计，以及预测后可选更新。 |
| [kalman_cv.cpp](../src/tracking/filter/kalman_cv.cpp#L20)、[kalman_cv.cpp](../src/tracking/filter/kalman_cv.cpp#L251) | NIS 推导重复，把平方量说成“几个 sigma”。建议集中说明平方马氏距离、所需噪声假设及理论卡方参考与实际关联样本的区别。 |
| [kalman_cv.cpp](../src/tracking/filter/kalman_cv.cpp#L375) | Joseph 说明称两个半正定矩阵之和必正定，且浮点下保证精确对称。建议说明正定条件、数值稳定性和实际的对称化复核。 |
| [covariance_validation.hpp](../src/tracking/filter/covariance_validation.hpp#L8) | positive_definite 只返回布尔，抛异常是 checked_covariance 的职责。建议按两个契约各写一处。 |
| [covariance_validation.cpp](../src/tracking/filter/covariance_validation.cpp#L4) | uᵀPu 被称为任意方向椭球径向长度平方。建议区分投影方差和径向交点，注明特征方向的对应关系。 |
| [covariance_validation.cpp](../src/tracking/filter/covariance_validation.cpp#L10) | 只有默认基础 R 对角，R+S 可非对角。一般的误差二次型也只有使用新息协方差时才是 NIS。建议按这两个口径重述。 |
| [target_tracker.cpp](../src/tracking/lifecycle/target_tracker.cpp#L44) | “每帧更新 state_time”遗漏无 profile、坏帧和数值回滚。建议限定为有 profile 且接受并提交的输入。 |
| [target_tracker.cpp](../src/tracking/lifecycle/target_tracker.cpp#L51)、[target_tracker.cpp](../src/tracking/lifecycle/target_tracker.cpp#L168) | “最近量测供下一帧 bootstrap”与确认后不再 bootstrap 不符。建议说明 seed 仅供确认前使用。 |
| [target_tracker.cpp](../src/tracking/lifecycle/target_tracker.cpp#L91)、[target_tracker.cpp](../src/tracking/lifecycle/target_tracker.cpp#L109) | “有量测就 predict + correct”遗漏首次 bootstrap 与最低 NIS 平局，后文“两个量测同分”也应限定最低分。建议把轨迹推进规则集中在一处。 |
| [tracking_io.cpp](../src/tracking/io/tracking_io.cpp#L32) | 示例协方差带省略号，不能直接解析。建议标为结构示意或补全 36 项。 |
| [measurement_adapter.cpp](../src/tracking/measurement/measurement_adapter.cpp#L47) | 出口注释只说 MARK，筛选实际服从可配置 source。建议写该来源的有效位置量测。 |

### 应用、测试、工具和文档

| 位置 | 问题与建议 |
|---|---|
| [offline_runner.cpp](../src/app/offline/offline_runner.cpp#L2)、[offline_runner.cpp](../src/app/offline/offline_runner.cpp#L105) | summary 被写成逐帧记录，位姿计数被写成帧数。建议说明 summary 汇总全程，位姿按观测条数累加，启用 Tag 后可一帧多条。 |
| [offline_runner.cpp](../src/app/offline/offline_runner.cpp#L99) | MARK 循环注释把 max(1,size) 称为下限，还说未检出附 reason。建议说明每帧至少一条观测，空检测时 estimate 与 reason 都为空。 |
| [offline_runner.cpp](../src/app/offline/offline_runner.cpp#L118)、[offline_runner.cpp](../src/app/offline/offline_runner.cpp#L165) | “检测断档只会越来越不确定”遗漏未建轨和数值回滚，结束时的文件名也被写死。建议注明只推进存活轨迹、目标名由配置指定。 |
| [video_exporter.cpp](../src/app/export/video_exporter.cpp#L27) | “调 release 就是完整 AVI”超出代码所能保证，尾部 path API 教程与发布语义无关。建议保留关编码器、同目录改名和回放验收完整性的说明。 |
| [debug_renderer.cpp](../src/app/rendering/debug_renderer.cpp#L38)、[debug_renderer.cpp](../src/app/rendering/debug_renderer.cpp#L83) | BGR {255,255,0} 是青色，注释称黄色。框重合只证明对当前角点的拟合。建议把颜色改对，限定重投影图层解释。 |
| [debug_renderer.cpp](../src/app/rendering/debug_renderer.cpp#L185) | 渲染函数内长篇调参把 NIS 均值约 3 当成 Q/R 正确的充分证据，并称 NIS 独立于滤波模型。建议移到标定说明并注明依赖模型 P/R、关联样本和独立真值。 |
| [run_report.cpp](../src/app/reporting/run_report.cpp#L45)、[run_console.cpp](../src/app/reporting/run_console.cpp#L7) | {name,count} 的行尾举了 frames 键，控制台说明把注入的 ostream 当成固定 std::cout。建议按实际 YAML 序列和输出接口举例。 |
| [tracking_profile.yaml](../config/tracking_profile.yaml#L3) | 通用方差公式、OpenCV YAML 教程与当前量级重复。建议只写每项单位、来源及初估条件，详细推导放导读。 |
| [tracking_profile.yaml](../config/tracking_profile.yaml#L11) | 静态抖动直接等同最终 R，距离幂次被写成普遍结论。建议说明误差传播假设并区分偏差、分歧和工况。 |
| [tracking_profile.yaml](../config/tracking_profile.yaml#L20) | 固定匀速被写成可确定 q 下界，第二个量测得到的速度又被称真值。建议明确匀速与加速工况、差分估计和独立速度真值的区别。 |
| [block3_fixture.hpp](../tests/support/detection/block3_fixture.hpp#L13)、[block3_fixture.hpp](../tests/support/detection/block3_fixture.hpp#L48) | “显式测试预算”对应默认角点配置，“非连续 ID”对应参数化合成场景。建议按当前夹具功能描述。 |
| [offline_chain_test.cpp](../tests/app/offline/offline_chain_test.cpp#L12)、[tag_detector_test.cpp](../tests/pose/tag/tag_detector_test.cpp#L1) | 前者仍提旧预算，后者标题留空括号。建议分别写清透视夹具、测试配置和 Tag 同库编码图的集成测试用途。 |
| [ippe_solution_count.cpp](../tests/validation/ippe_solution_count.cpp#L1)、[compare_pose_boundary.py](../tests/validation/compare_pose_boundary.py#L2) | “同一求解器”实际是独立调用同一 OpenCV 算法与共享复核。比较脚本的 current 指历史 schema 6 边界。建议把适用版本和用途写明确。 |
| [calibration_result.cpp](../tools/calibration/solution/calibration_result.cpp#L6)、[calibration_result.cpp](../tools/calibration/solution/calibration_result.cpp#L41) | “平均重投影误差”易误会成欧氏距离算术平均，实际为二维 RMSE。来源映射只在内存。建议明确口径及输出字段。 |
| [calibration_detection.cpp](../tools/calibration/observation/calibration_detection.cpp#L5)、[calibration_video.hpp](../tools/calibration/frames/calibration_video.hpp#L30) | 标定板点被泛称世界坐标，空帧表只说不抽帧。建议写板坐标，并说明空帧表产生零抽样、后续 CLI 检测失败。 |
| [tools/calibration/CMakeLists.txt](../tools/calibration/CMakeLists.txt#L1) | “同时供标定工具消费”暗示多个调用者。建议按当前唯一链接入口描述。 |
| [flow-guide.md](flow-guide.md#L13) | 流程图只标出一次安装映射，省略 float 发布后的屏幕顺序与 PnP 物理索引回查。建议在两个边界标出顺序与 orientation。 |
| [tools/INDEX.md](tools/INDEX.md#L3) | 已归档的 Block3/4/5 工具和 CLI 被写成现行入口，tools/audit、tools/block3_fixture 路径在当前树不存在。建议标出适用提交，独立列出现行工具。 |
| [VISION_REFACTOR_INDEX.md](VISION_REFACTOR_INDEX.md#L3)、[g4.md](g4.md#L3)、[diagnostics_schema.md](diagnostics_schema.md#L5) | 阶段文档的“当前”指旧版，例如空 profile、Unique、时序可观测性协议。建议给各文档加冻结版本标识。 |
| [dataflow-interfaces.md](dataflow-interfaces.md#L5)、[lean-changes.md](lean-changes.md#L32)、[block3.md](block3.md#L11)、[block4_acceptance.md](block4_acceptance.md#L169) | 旧 YAML、lib 和 include/mark 等相对链接已失效。建议把历史复现链接固定到对应提交。 |
| [evidence/g4/source-snapshot](evidence/g4/source-snapshot/)、[ref/G4-Codex.md](ref/G4-Codex.md)、[validation/pose-status/README.md](validation/pose-status/README.md) | 冻结源码和数据中的旧字段属于其阶段，部分内链因归档位置或缺附件失效。建议在归档索引补复现范围和缺失附件说明。 |
