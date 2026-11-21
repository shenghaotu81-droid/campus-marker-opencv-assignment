# 视觉管线最终精简修改说明

基线为 `3286032`，交付分支为 `refactor/vision-lean-fixed-final`。B1 按确认结果保留独立的 `source_` 筛选，默认 MARK 跟踪继续排除 AprilTag 位姿。

## 源码和配置

| 文件 | 改动 |
|---|---|
| `include/vision/types/pose_types.hpp` | `CaptureStamp` 删除 `exposure_us_`、`time_uncertainty_s_`、`session_id_`、`camera_id_`、`clock_domain_`、`pixel_domain_`，仅含 `frame_id_` 与 `received_us_`。`PoseObservation` 删除 `target_id_`。 |
| `include/vision/config/pose_config.hpp`、`src/config/pose/pose_config.cpp`、`config/pose.yaml` | 分别删除 `mark_target_id_` 成员、读取代码及 `geometry.target_id`。 |
| `include/vision/config/tracking_config.hpp`、`src/config/tracking/tracking_config.cpp`、`config/tracking.yaml` | 分别删除 `target_class_`、`stamp_uncertainty_semantics_` 成员、读取代码及对应配置键。 |
| `include/vision/pose/pose_estimator.hpp`、`src/pose/estimation/pose_estimator.cpp` | `estimate_planar` 去掉目标 ID 参数、赋值、空 ID 检查及已无输入的相机 ID 比较。标定图像尺寸与像素四边形仍检查，仍用原 reason。`estimate_mark` 删除帧戳、检测状态、检测索引和映射的四项指定验证。帧戳检查整块删除，涵盖 A 组指到的曝光回退表达式。空检测列表仍产生空 MARK 观测，检测非空时由调用方保证索引和排列有效。 |
| `src/pose/tag/tag_detector.cpp` | `estimate_exit` 去掉无效边长结果的 `target_id_` 赋值及平面求解调用的 ID 实参。 |
| `src/pose/io/pose_io.cpp` | 时间戳写出仅含十进制字符串 `frame_id` 和 `received_us`，位姿观测去掉 `target_id`。跟踪写出共用该时间戳函数，因此输入和最近实测时间戳同步精简。流头及版本号沿用 schema 6 和 2。 |
| `src/detection/pipeline/detector.cpp` | 删除 `original_size_` 及其更新和清空、尺寸比较、尺寸变化触发的 reset 和 `INPUT_CHANGED`。帧序所用 `last_input_` 和公开 `Detector::reset()` 继续使用。 |
| `src/tracking/filter/kalman_cv.hpp`、`src/tracking/filter/kalman_cv.cpp` | `bootstrap` 去掉 `bound` 和 `position_bound`、对应验证及整个有界初始化分支，名义公式保留。`predict` 去掉 `bound` 参数及校验、`upper` 和 Young 不等式分支，Q 以 `dt` 计算。 |
| `src/tracking/measurement/measurement_adapter.hpp`、`src/tracking/measurement/measurement_adapter.cpp` | 删 `same_sample` 和 `time_augmented_noise` 的声明与实现，`sample_time` 直接返回 `received_us_`。两遍量测筛选均去掉目标类别比对，第二遍去掉同采样比较，保留 source 和重复索引筛选。 |
| `include/vision/tracking/target_tracker.hpp`、`src/tracking/lifecycle/target_tracker.cpp` | 删除 `TargetTracker::reset()` 的声明及实现。删 `time_bound`、`same_context`、`CONTEXT_RESET`、曝光和误差界校验、预测时界及上帧临时副本、初始化的时界实参和 R 增广循环。 |
| `src/app/offline/offline_runner.cpp` | 删除四个时间戳标签的赋值，继续从帧号和 FPS 生成微秒时间戳。 |

## 测试、文档与依赖

| 文件 | 改动 |
|---|---|
| `tests/tracking/lifecycle/tracking_contract_test.cpp` | 夹具适配精简字段，删除有界预测、重叠时界初始化、上下文重置及非法曝光用例。保留零 dt 初始化拒绝测试。 |
| `tests/validation/tracking_replay.cpp` | 仅读取时间戳剩余两字段，去掉目标 ID 读取。可读取精简前后两份 schema 6 观测。 |
| `tests/pose/estimation/pose_contract_test.cpp` | 调用适配平面求解签名与精简时间戳夹具，正常未检出仍有测试。 |
| `tests/support/pose/pose_test_scene.hpp`、`tests/pose/tag/tag_detector_test.cpp` | 位姿时间戳夹具仅填帧号、微秒时间，去掉无用的配置实参，Tag 测试更新调用。 |
| `tests/app/offline/offline_chain_test.cpp` | 删除 `time_bounded` 断言，有限位置与 `qualified_for_g5` 断言保留。 |
| `README.md`、`docs/flow-guide.md`、`docs/INDEX.md` | 同步当前配置、输入契约、时间戳、序列化及验收入口。 |
| `docs/VISION_LEAN_REPORT.md`、`docs/validation/pose-status/README.md` | 标注上一版报告的适用版本，将依赖旧 `target_id` 与完整时间戳的历史比较步骤固定到 `4d4aac9`。 |
| `docs/validation/final-lean/{observations,tracks}.yaml.gz` | 将此次回放的精简版观测和轨迹重新生成并归档。 |

构建配置与库依赖沿用现有配置。C 组的角点凸性重检、`normalized_turn`、`Point2f` 和 AprilTag 默认参数均保留。

## 随删除项移除的注释

| 文件 | 移除的说明 |
|---|---|
| `pose_types.hpp`、`pose_config.hpp`、`tracking_config.hpp` | 帧戳标签与像素域、曝光与时间误差字段、MARK 的目标标识、目标类别比对与时界语义开关。 |
| `kalman_cv.cpp` | 两端和单帧时间界、有界初始化分支内的最小间隔、MSE 包络与协方差放大、以 `bound == 0` 区分分支的标签、Young 不等式预测分支。 |
| `target_tracker.cpp`、`target_tracker.hpp` | 上下文比较和重建分支、时界取值、曝光与误差界检查的行尾解释、上帧时间界临时副本、预测及初始化的时界实参、R 增广循环、公开 reset 的实现及说明。 |
| `measurement_adapter.cpp` | 曝光优先及 `value_or` 用法、同采样比较、R 增广函数、单独说明目标类别与同采样过滤的两行。 |
| `offline_runner.cpp` | 会话及相机 ID 赋值说明、仅说明将来填曝光与时间误差字段的注释。 |
| `pose_estimator.cpp` | 对应整块帧戳匹配、索引验证与排列验证的说明。 |
| `pose_io.cpp`、`tag_detector.cpp` | 单独说明目标 ID 写出和 Tag 目标 ID 字符串构造的注释。 |
| `detector.cpp` | `original_size_` 成员、尺寸比较、按尺寸清状态及写诊断、清空该尺寸成员的说明。 |

## 构建与回放验收

- `cmake -S src/tushenghao -B build`、`cmake --build build` 成功。Release 构建的 `ctest --test-dir build` 为 **11/11**。
- 用两版各自构建的 `marker_app --video` 回放同一份 `data/raw/marker_video.avi`。均处理 1676 帧，1088 检出、1088 ESTIMATED、588 UNAVAILABLE、1088 MEASURED、588 COASTING。
- 从基线两份 YAML 中逐行去掉 `session_id`、`camera_id`、`clock_domain`、`pixel_domain`、`exposure_us`、`time_uncertainty_s` 和 `target_id`，再用 `cmp` 比较。两份文件的剩余内容逐字节一致，包括 1676 组位置、1675 组速度、1676 个协方差 P 和 1087 个 NIS。两版叠加 AVI 逐字节一致，汇总的计数和事件一致，`elapsed_s` 随实测运行时间变化。
- 用新版 `tracking_replay` 分别读取删前和删后的观测，均复现新版轨迹文件。在首帧加入与 MARK 使用同一索引的 APRILTAG 观测后复跑，轨迹文件仍逐字节相同。
- 精简输出通过 `gzip -n -c` 生成归档，解压后用 `cmp` 核对原始输出。以下为解压后的 SHA-256。

| 期望输出 | SHA-256 |
|---|---|
| [observations.yaml.gz](validation/final-lean/observations.yaml.gz) | `5286f978ce96dc23ee8f626e654c547099f6481c5a9049e95f7772c85d10d2b8` |
| [tracks.yaml.gz](validation/final-lean/tracks.yaml.gz) | `5ffe3ad9d3a28827eb29dfb71df7880043443dea8f157f519b9adc1842fec2e2` |
