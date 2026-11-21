# 视觉管线最终精简第二轮修改说明

基线为 `7f818ec`，交付分支为 `refactor/vision-lean-fixed-final`。以下序号对应首轮报告和第二轮指令的裁决。仓库扫查发现见[第二轮报告](FINAL_LEAN_REPORT_2.md)。

## 按文件记录

| 文件 | 裁决与处理 |
|---|---|
| `include/vision/config/tracking_config.hpp` | 混合 1，配置总览保留 source 和可选 profile。建-1，说明 NIS 排序和最小分数平局跳过更新。建-5，初速方差定位为初始 P 的速度块对角项。 |
| `include/vision/detection/detector.hpp` | 混合 2，Impl 行尾只说明配置和帧序状态。 |
| `src/detection/pipeline/detector.cpp` | 混合 3，裁掉流程、构造、状态提交和 reset 四处的尺寸说明，保留帧号、时间戳和 reset 的作用。 |
| `include/vision/pose/pose_estimator.hpp` | 混合 4，按当前 MARK 返回字段和平面求解参数说明接口，保留重排角点和 `last_input_` 的 pimpl 对比。 |
| `src/pose/estimation/pose_estimator.cpp` | 混合 5，更新 MARK 观测身份、空观测、平面求解实参与返回字段，以及通用求解的来源和几何检查。待-4，将 `STAMP_CAMERA_OR_QUAD_INVALID` 改为 `IMAGE_SIZE_OR_QUAD_INVALID`，触发条件仍为图像尺寸与标定不一致或像素四边形非法。 |
| `src/pose/io/pose_io.cpp` | 混合 6，说明 YAML 帧戳 map 只写两个十进制字符串。位姿观测总览移除目标 ID，保留可选 estimate 和 reason。待-4，同步 reason 注释示例。 |
| `src/pose/tag/tag_detector.cpp` | 混合 7，平面位姿调用注释列出模型点、角点、图像尺寸和帧戳。 |
| `src/app/offline/offline_runner.cpp` | 混合 8，保留帧号/FPS 推算微秒时间及原图畸变像点说明，去掉旧时间戳标签和将来填字段的说法。建-1，汇总说明使用 MEASURED、COASTING 和实际事件，平局跳过量测更新。 |
| `src/tracking/filter/kalman_cv.hpp` | 混合 9，两点初始化与预测保留速度、交叉协方差和连续白加速度 Q。建-1/2，描述 NIS 排序和当前新息字段，删旧版本检查和标准差 getter 的说明。 |
| `src/tracking/filter/kalman_cv.cpp` | 混合 10，保留位置/R/dt 输入要求和名义 Q。建-3，核对本机 `opencv2/core/mat.hpp`，同步协方差写回、初始化和修正说明。Matx/Vec 转 Mat 默认 `copyData=true`，显式 `false` 共享原存储。 |
| `src/tracking/measurement/measurement_adapter.cpp` | 混合 11，注明离线采样时刻及两遍来源、估计、重复索引筛选。建-6，注明位置分歧 S 由位姿求解计算，跟踪使用 `R_profile + S`。 |
| `src/tracking/lifecycle/target_tracker.cpp` | 混合 12/13，更新 `elapsed`、存活轨迹与上一帧状态、负时间检查、乱序检查、状态提交、预测及量测适配说明。待-3，删除单独的接收时间倒退比较，因为 `sample_time` 就是 `received_us_`，前面的 `<=` 已覆盖。建-1，同步状态副本推进和 NIS 平局说明。 |
| `src/tracking/io/tracking_io.cpp` | 建-4，给示例的 `last_measurement_stamp.frame_id` 和 `received_us` 加引号。 |
| `include/vision/types/pose_types.hpp` | 待-1，删除失去引用的 `PixelDomain` 枚举。 |
| `include/vision/config/pose_config.hpp`、`src/config/pose/pose_config.cpp`、`config/pose.yaml` | 待-2，删除 `CameraModel.camera_id_`、读取及 YAML 键。它在基线已无运行期消费者。 |
| `docs/flow-guide.md` | 同步相机配置字段和新诊断名。 |
| `docs/INDEX.md`、`docs/FINAL_LEAN_REPORT.md` | 索引关联第二轮交付，首轮报告标记为 `7f818ec` 的裁决来源。 |

在现行源码、头文件、配置、工具和测试中查找了旧诊断名、删除的枚举和相机 ID。新诊断作为 reason 字符串由现有位姿 YAML 写出，应用沿用原有统计和显示行为。旧文本留在对应的历史说明和冻结证据中。构建依赖和回放 reader 无需调整。

## 验证结果

- `cmake --build build` 成功，Release 构建的 `ctest --test-dir build` **11/11**。
- 用临时探针分别传入错误图像尺寸和退化四边形，两次均返回空估计及 `IMAGE_SIZE_OR_QUAD_INVALID`。
- 以基线和新版的正式 `marker_app --video` 分别处理同一份 `data/raw/marker_video.avi`，各 **1676 帧**。两份观测 YAML、两份跟踪 YAML 及两份叠加 AVI 经 `cmp` 逐字节一致。跟踪流含 1676 组位置、1675 组速度、1676 个 P 和 1087 个 NIS。摘要除实测 `elapsed_s` 外一致，仍为 1088 MEASURED、588 COASTING。
- 基线输出与首轮归档解压后逐字节一致。新版 `tracking_replay` 从新版观测重建的轨迹也逐字节一致。本轮删的是未消费的配置字段和枚举，回放输出字段及期望文件沿用首轮结果。
