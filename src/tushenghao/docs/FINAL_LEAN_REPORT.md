# 视觉管线最终精简待审报告

## 混合注释

| 位置 | 涉及的已删内容与保留逻辑 |
|---|---|
| [tracking_config.hpp](../include/vision/config/tracking_config.hpp#L21) | 配置字段总览同时列出目标类别、时间界语义，以及仍使用的 source 和 profile。 |
| [detector.hpp](../include/vision/detection/detector.hpp#L30) | Impl 行尾注释同时讲已删原图尺寸和仍使用的配置、帧序状态。 |
| [detector.cpp](../src/detection/pipeline/detector.cpp#L3) | 文件开头流程、构造函数行尾、写入上一帧状态前、reset 声明前的四段，分别混有尺寸保存、成员初始值、尺寸变化 reset，以及保留的检测流程、帧序记录和 reset。 |
| [pose_estimator.hpp](../include/vision/pose/pose_estimator.hpp#L17) | 返回字段、MARK 调用方式、平面求解参数分别混有已删帧戳字段、目标 ID 或 ID 实参，与仍使用的帧戳、估计、来源、索引、模型及像点。私有成员区的 pimpl 对比另混有 `original_size_` 和 `last_input_`。 |
| [pose_estimator.cpp](../src/pose/estimation/pose_estimator.cpp#L28) | MARK 的身份、无检测分支、平面求解调用、返回字段四处，同时说明目标 ID 或已删状态拒绝，以及保留的 stamp/source/index、空观测和 PnP。平面求解的身份和输入校验两处，混有目标 ID 和相机 ID 验证，以及保留的 stamp/source、图像尺寸和像素四边形验证。 |
| [pose_io.cpp](../src/pose/io/pose_io.cpp#L5) | 时间戳函数说明和字段清单混有六个已删字段、空 optional 写法，以及保留的 map 和两种十进制字符串。位姿写出函数总览同时讲目标 ID 和保留的其他输出。 |
| [tag_detector.cpp](../src/pose/tag/tag_detector.cpp#L213) | 平面求解调用说明同时列出已删的目标 ID 实参以及模型点、角点、图尺寸和 stamp。 |
| [offline_runner.cpp](../src/app/offline/offline_runner.cpp#L71) | 推断时间戳注释兼述帧号/FPS 和将来填曝光与误差字段。时间戳字段总览兼述保留字段。原时钟域和像素域赋值的行尾说明还解释离线名义时间及 PnP 所用畸变像素，注释文字原样保留为独立行。 |
| [kalman_cv.hpp](../src/tracking/filter/kalman_cv.hpp#L25) | 两点初始化与预测说明分别混有时界分支、Young 项，以及保留的速度公式、交叉协方差、连续白加速度 Q。 |
| [kalman_cv.cpp](../src/tracking/filter/kalman_cv.cpp#L125) | 初始化校验、预测间隔校验、Q 说明分别混有已删的时界校验或 `upper`，以及保留的位置/R/dt 合法性、名义 Q 公式。 |
| [measurement_adapter.cpp](../src/tracking/measurement/measurement_adapter.cpp#L1) | 文件头兼述已删同采样和目标身份比较，以及保留的来源筛选、位置和噪声适配。采样时刻说明混有旧回退行为和离线名义时间。两遍筛选的说明兼述已删 target 及步骤数量，以及保留的 source、估计存在和重复项处理。 |
| [target_tracker.cpp](../src/tracking/lifecycle/target_tracker.cpp#L16) | 匿名命名空间注释的 helper 数量混计已删的两个函数和保留的 `elapsed`。状态说明中的 live 和 last_input 混有已删 reset/CONTEXT_RESET 及保留的首次输入与帧序语义。 |
| [target_tracker.cpp](../src/tracking/lifecycle/target_tracker.cpp#L65) | 时间戳校验、乱序检查行尾、接受输入、预测和量测适配五处，分别混有曝光/误差界、曝光优先、`previous`、时界和 R 增广或目标类别，与保留的负时间校验、采样时序、输入提交、预测时间及位置量测转换。 |

## 清单之外的待裁决项

| 位置 | 发现 |
|---|---|
| [pose_types.hpp](../include/vision/types/pose_types.hpp#L11) | `PixelDomain` 枚举随帧戳字段删除失去现行代码引用。B6 只指定字段，枚举仍可作为公开类型使用。 |
| [pose_config.hpp](../include/vision/config/pose_config.hpp#L39) | `CameraModel.camera_id_` 仍从 `pose.yaml` 读取，生产链已无消费者。它是相机模型配置字段，B6 指定的是 `CaptureStamp` 同名字段。 |
| [target_tracker.cpp](../src/tracking/lifecycle/target_tracker.cpp#L78) | `sample_time` 现在等于 `received_us_`，前面的采样时刻不递增比较已覆盖随后单独比较的接收时刻倒退。 |
| [pose_estimator.cpp](../src/pose/estimation/pose_estimator.cpp#L75) | `STAMP_CAMERA_OR_QUAD_INVALID` 现只由标定图像尺寸或四边形触发。可评估日后是否单独修改诊断字符串及其消费者。 |

## 其余注释建议

| 位置 | 问题与建议 |
|---|---|
| [tracking_config.hpp](../include/vision/config/tracking_config.hpp#L8)、[kalman_cv.hpp](../src/tracking/filter/kalman_cv.hpp#L11)、[target_tracker.cpp](../src/tracking/lifecycle/target_tracker.cpp#L31)、[offline_runner.cpp](../src/app/offline/offline_runner.cpp#L121) | 多处使用“NIS 门”“做门控”或“NIS 拒绝”，离线汇总的注释还提到已删的 LOST。建议描述 NIS 排序及平局跳过更新，状态列出 MEASURED、COASTING。 |
| [kalman_cv.hpp](../src/tracking/filter/kalman_cv.hpp#L32) | `InnovationResult` 已无 owner/revision，`CvFilter` 也无 `position_std()`、`velocity_std()`。建议删去对应说明，保留当前返回字段和状态读法。 |
| [kalman_cv.cpp](../src/tracking/filter/kalman_cv.cpp#L57) | Matx 转 Mat 的说明将 OpenCV 默认 `copyData=true` 写成了 `false`。初始化和修正处也混淆默认拷贝与显式 `false` 的共享效果。建议以本机 OpenCV 头文件的接口为准逐处核对。 |
| [tracking_io.cpp](../src/tracking/io/tracking_io.cpp#L39) | 示例中的 `last_measurement_stamp.frame_id` 与 `received_us` 写成整数。建议加引号，与实际十进制字符串一致。 |
| [tracking_config.hpp](../include/vision/config/tracking_config.hpp#L15) | 初始速度方差标成 Q。建议称为初始 P 的速度块对角项，Q 留给预测过程噪声。 |
| [measurement_adapter.cpp](../src/tracking/measurement/measurement_adapter.cpp#L47) | 量测构造说明将 S 误归为检测模块并附“待删”。建议说明 S 由位姿求解计算，跟踪用于 `R_profile + S`。 |
