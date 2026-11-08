# Codex 批量改动指令 —— 视觉管线瘦身（通读第 1–4 节）

**分支**：`refactor/vision-lean-fixed`（当前 @ `edc9477`，2026-11-08 晚用户刚推；此前 4d4aac9 之后新增的 edc9477 "[refactor] 通读并指出简化改进部分 整理部分结构" 仅做了格式整理与少量中文注释补充，未做实质删减——下面清单的改动项均未落地，仍以 edc9477 为基线执行），在此分支上改，提交格式 `[<scope>] <中文描述>`。
**范围**：`src/tushenghao/` 源码 + `config/` YAML + `src/tushenghao/docs/flow-guide.md`。不碰 `docs/` 证据目录、不碰 G-02/G-03（板端实测数据）、不碰板端移植。

---

## 一、总原则（先读，再动手）

1. **“防我自己而不是程序”**：YAML 配置全由用户亲手填，他不会填错。触发场景只能是“用户写错配置”的校验一律删除。只保留防程序崩坏 / 坏数据 / 非法输入的检查（如视频打不开、卡尔曼数值正定性）。
2. **只做减法与统一，不增加复杂度**：不引入新抽象、新层次、新参数。合并只针对单使用者文件，抽象需 ≥2 个使用者。
3. **不要全信下面这份清单**：清单里的每项判断（哪些冗余、哪些可合并、哪些可删）只是 Oyster 的建议，**不一定全对**。动手前先独立思考每项是否正确，确认无误再改；有疑问的先停下，在改动报告里写明疑问，不要硬改。改完后顺便通查全库，看有没有**同类问题**（同种冗余、同种不对称）或**别的问题**没列进来，一并处理。
4. **修得了就修，修完重验继续**：改动后构建/测试失败，能修的自己修、修完重验继续；只有修不了、或修复会改变行为/语义/阈值/门状态时，才停下报告。
5. **文件结构要整理好**：改动涉及删文件、移文件、改名、拍平目录的，做完后检查文件结构是否整齐——不留空目录、不留孤儿文件（删了 .cpp 别留 .hpp 悬空）、CMakeLists 与 include 与新结构同步、目录层级与模块划分一致。改完自己过一遍目录树确认。

---

## 二、改动清单

### A. 配置加载（detector / pose / tracking 三处同理）

1. **删全部“防用户”校验**：`fields()`（未知/重复 key 检查）、`validateDetectorConfig`、`validateOfflineConfig`、`validate_pose_config`、`validateTagConfig`、`CaptureStamp::valid()`。`tag_config_validation.cpp/hpp` 两个文件随 `validateTagConfig` 一并删。
2. **删 `schema_version`**：三个 YAML 里的字段全删，loader 里的 `!= 2` 检查全删（marker_geometry.yaml 的 schema_version: 1 本就无人校验，一并删）。
3. **value_reader 瘦成干净接口**：保留 `value_reader.hpp` 作为三处 loader 共用的读取接口，只做“取节点 → 转 C++ 值”。删：`required` 缺字段检查、类型检查（`isInt`/`isString`/`isReal`/有限数）、`fields()`、`NumericPolicy` 枚举（`number()` 改为 plain `static_cast<double>`）。
4. **删 `config_error.hpp`**：`ConfigError` 只是 `std::runtime_error` 换名，从未被单独 catch。detector 改抛 `std::runtime_error`，与 pose/tracking 一致。
5. **重命名**：detector 的 `config.cpp`/`config.hpp` → `detector_config.cpp`/`detector_config.hpp`（同步改 CMakeLists 与 include）。
6. **命名空间与函数名统一**：detector 的 `namespace mark` + `loadConfig` → `mark::detector` + `load_detector_config`，与 pose（`mark::pose` / `load_pose_config`）、tracking（`mark::tracking` / `load_tracking_config`）对齐。**顺手通查全库其他同类不对称，一并改掉。**
7. **`G2Config` → `PoseConfig`**（唯一用阶段代号命名的 struct；CMake 的 `MARK_BUILD_G2` 不在改名范围，见 A8）。
8. **删 `MARK_BUILD_G2` / `MARK_BUILD_G4` 开关及全部 `#ifdef VISION_HAS_POSE` / `VISION_HAS_TRACKING`**：始终编完整管线。范围：`src/app/CMakeLists.txt`、`src/config/CMakeLists.txt`、`src/pose/CMakeLists.txt`、`src/tracking/CMakeLists.txt`（含 option 定义与“G4 要求 G2”的 FATAL_ERROR）；源码仅 offline_runner.cpp（11 个 #ifdef 块）。注意 tracking 块用到的 `stamp` 在 pose 块里声明，删 #ifdef 后须保证 `stamp` 始终可见。
9. **删 offline_runner 的冗余报错**：时间戳溢出检查、`OUTPUT_DIRECTORY_EXISTS`、`POSE_OUTPUT_OPEN_FAILED`、`TRACK_OUTPUT_OPEN_FAILED`、`INPUT_HAS_NO_FRAMES`。**只留 `INPUT_OPEN_FAILED`**（视频打不开是真实可能发生的失败）。`INPUT_FPS_INVALID` 未拍板：自己判断 FPS 无效时（0 或非有限）是否还有保留价值，写进改动报告。
10. **`app_config.hpp` 位置**：目前在 `src/config/application/`，不在 `include/vision/config/`。挪到 `include/vision/config/`（或在 flow-guide 注明），自己判断哪种更合适。
11. **删 `tools/common/`**：只剩一个 CMakeLists.txt（一行注释的空壳）。同时删 `tools/CMakeLists.txt` 里的 `add_subdirectory(common)`。

### B. 配置文件结构

12. **marker_geometry.yaml 并入 pose.yaml**：该文件唯一用途是给 PnP 提供 4 个模型点（L0.v0=[0,0]、M1.v1=[80,0]、L2.v0=[80,80]、L3.v0=[0,80]，即 80×80 方框四角），anchor/area/S1a/S1b/其余顶点无人读。pose.yaml 增加 `geometry.model_points: [[0,0],[80,0],[80,80],[0,80]]`（保留 `source_unit: "mm"`），删 marker_geometry.yaml、删 load_points 的多边形查找逻辑、删 marker_geometry.hpp 的 ModelCornerContract。**条件：现有流程不受影响（4 个点数值相同，PnP 计算不变）。**
13. **output 配置独立成 offline.yaml**：从 detector.yaml 的 output 段拆出，新建 `config/offline.yaml`，含三项：`run_directory: "outputs/marker"`（写明默认值）、`video_filename: "overlay.avi"`（可配，改名可避免两次运行覆盖）、`overlay: true`（bool 开关，默认 true，见 B14）。detector.yaml 只留检测参数（preprocess、geometry、corner）。main.cpp 改为两次加载（detector.yaml + offline.yaml，同一 config 目录）。
14. **视频格式固定 MJPG，删 `video_fourcc` 参数**：VideoWriter::open 处写死 MJPG（现代码已写死，只是参数传了没用）；删 YAML 里的 video_fourcc、OfflineRunConfig 里的 video_fourcc 字段、video_exporter open() 的 fourcc 检查。文件名须为 .avi 后缀，不另加校验（用户不会写错）。
15. **overlay 开关接线**：offline_runner.cpp 把 exporter 的 open/write/finish（含 renderDebugFrame）包进 `if (app.offline.overlay_)`。注意：offline_runner 是**调用方**，只包调用处，VideoExporter 类本身（video_exporter.cpp/hpp 里的实现）不动。

### C. 命令行（main.cpp / cli_options.hpp）

16. **删 `--config`**：程序固定用默认 config/detector.yaml（及同目录 pose.yaml/tracking.yaml、offline.yaml）。
17. **删 `--camera`**：只用视频文件。main.cpp 删 --camera 入参、decimal_uint64 解析、"camera:N" 拼装、--video/--camera 二选一检查；offline_runner 删 "camera:" 前缀判断、`capture.open(N)` 分支、时间戳的相机/视频双分支（只留帧序号/FPS）、clock_domain 的相机/视频双分支（固定 "video-fps"）；cli_options.hpp 的 `decimal_uint64` 随之无调用方，一并删。**时间戳字段本身保留**（G4 需要 dt、跟踪需要排序、G2 需要 stamp 匹配；板端以后用真实驱动时间）。
18. **cli_options 结构瘦身（校验本身全部保留）**：终端输入校验不是冗余（用户在终端敲命令确实可能打错），Unknown / MissingValue / Duplicate 检查、reject_option_value、decimal_uint64 全数字检查（随 --camera 删除而无调用方，见 C17）均保留。只删结构冗余：死的 `flags` 参数 + `DuplicateFlag` 枚举值（无任何 flag 选项）、`Fail` 模板回调（仅 1 个调用方，改为直接抛错的普通函数）、首行 "A01/V02" 注释（见 D1）。只剩 --video 一个 valued 选项后，是否进一步把解析直写为 argc 检查，自己定。
19. **最终命令行**：只剩 `--video VIDEO` 一个输入，用法 `marker_app --video VIDEO`。

### D. 注释与命名（全库扫）

20. **删 G1/G2/G3/G4 阶段代号**：代码与注释里的阶段代号一律改用描述性名称（G2→Pose、G4→Tracking；G2Config 已在 A7）。包括注释、标识符、报错文案里的 "G2"/"G4" 字样。CMake 开关 MARK_BUILD_G2/G4 是删除不是改名（见 A8）。
21. **清版本历史注释（全库扫）**：凡注释含审计编号（A/Q/P/K + 数字，如 A01/V02、Q01/Q03、Q02、Q03/P03、Q05/K06、R2:）、“原/原来/旧实现”、"[lean]"、commit hash 等版本迭代痕迹的，一律改写为说明**当前行为**的注释或直接删除。已知规模：编号类 ≥6 处、"原/原来/旧"类 ≥19 处；kalman_cv.cpp 的 "// R2：原有界初始化遗漏观测时偏导致的位置位移项。" 亦在内。
22. **补中文注释（板块/流程说明，不是改动记录）**：上一轮改动中文注释极少（4d4aac9：757 行仅 18 行中文；a392bd4：2634 行仅 65 行）。**本次改动触及的函数/模块，补上中文注释，说明该板块在整个流程里是干什么的**（输入是什么、输出是什么、这一步的用途、数据流向哪里，意思层即可）。**注释写的是代码本身的职责，不是这轮改了什么**——不写“原来……”“改成……”“删除了……”“本次……”，不写审计编号、不写改动历史、不写只针对某一轮的说明。举例：好注释是“这一步把检测到的角点按屏幕顺序排序，供 PnP 使用”；坏注释是“这里原来用 X 排序，现在改成 Y”。
23. **命名风格统一（全库扫）**：驼峰与下划线混用一并统一（如 `writeRunSummary` vs `write_run_console`）。自己通查，不只改这一对。
24. **types/ 整合（只做减法与统一，不增加复杂度）**：
    - `decode_result.hpp`（仅 1 个使用者）→ 并入 detector_types.hpp；`geometry_types.hpp`（WhiteComponent，仅 1 个使用者）→ 并入 prepared_image.hpp。
    - `corner_types.hpp`（4 个使用者）与 `prepared_image.hpp`（5 个使用者）**保留为独立文件**（不要按行数合并）。
    - types/detection/ 子目录只剩 2 个文件时是否保留，自己判断。
    - 文件名统一（5 个 `_types.hpp` vs decode_result/prepared_image/marker_geometry 三个例外）、文件名与内容对齐、命名空间统一（pose/tracking 用 mark::pose/mark::tracking，detector/detection/marker 用 plain mark）、成员名下划线后缀统一、删 `CaptureStamp::valid()`、评估 `DecodeStageResult` 与 `FrameResult` 合并（后者只多 frame_id/timestamp_us）。

### E. math/ 解散

25. **math/ 整个删除**：`positive_definite`（声明 + 实现）移入 tracking/（kalman_cv.cpp 里的 4 处检查**保留**，是防浮点舍入的数值安全网，不是防用户；tracking_config.cpp:70 那处防用户型的删）；`segmentDistance`（仅 1 个使用者 contour_topology.hpp:55）移入 detection/；`include/vision/math/` 与 `src/math/` 全部删除，不留空壳。测试里对 positive_definite 的 2 处调用随之挪到 tracking/。

### F. app / observability

26. **video_exporter 瘦身**：`finish()` 删逐帧读回验证（帧数/尺寸/FPS 全验才改名）；`open()`/`write()` 的防御性检查删（尺寸/FPS/fourcc/路径已存在/isOpened/finished/空图/类型/尺寸检查）。
27. **run_report 搬家**：`src/observability/reporting/run_report.cpp/hpp` 移入 `src/app/reporting/`，删 `src/observability/` 目录。run_report.cpp 删 `SUMMARY_WRITE_FAILED` 打开检查。**核实** `std::to_string` 模式：run_report.cpp、pose_io.cpp（frame_id/received_us/exposure_us/detection_index）、tracking_io.cpp（track_id/state_time_us）三处一致地把数字转字符串再写 YAML——核实是刻意选择还是系统性写法问题（OpenCV 可直接写整数），有问题就改，没问题就留着并在报告里写明。
28. **debug_renderer（69 行）、run_console（17 行）基本干净**，不动。

### G. detection/ 结构（Section 2）

29. **拍平嵌套**：detection/ 现为两层嵌套（corners 下 5 子目录、geometry 下 2、pipeline 下 3，共 11 个目录），pose/tracking 均为一层平铺。把 corners/geometry/pipeline 拍平为一层，与 pose/tracking 一致。
30. **合并单使用者小文件**：`frame_sequence.cpp`（19 行，validateFrameStamp，仅 detector.cpp 调用）→ 并入 detector.cpp；`corner_resolver.cpp`（27 行，仅 decode_stage.cpp 调用）→ 并入 decode_stage.cpp；`detection_publication.cpp`（27 行，仅 decode_stage.cpp 调用）→ 并入 decode_stage.cpp。保留独立文件：corner_edge_fit（96 行，核心拟合）、match_quad（145 行）、geometry_observation（73 行）、corner_observation（60 行）、preprocess（75 行）。`screen_order.cpp`（37 行，2 个调用方）函数保留，不必独占目录。
31. **preprocess.cpp 删 3 处重复校验**：① 格式检查（empty/dims/CV_8UC3）与 detector.cpp:29 重复；② work_width/work_height ≤ 0 与 validateDetectorConfig 重复；③ threshold <0||>255 与 validateDetectorConfig 重复。删完后 preprocess 只做 resize + scale + 构造 PreparedFrame。**注意**：加载时删 validateDetectorConfig，运行时也不验——同一条原则，不留一半。
32. 其余文件内部逻辑干净，不用动（match_quad 的六尺度 {1,.5,.75,1.25,1.5,2} 在两处各写一次，可顺手抽为命名常量；normalizedTurn 在 cornerTopology 内算两次，可顺手缓存；geometry_observation 空白行过多，顺手清理）。

### H. pose / tracking minor 项（Section 3/4）

33. pose/（384 行）与 tracking/（534 行）基本干净，无重大冗余。仅处理以下 minor 项：
    - std::to_string 模式：见 F27（与 run_report.cpp 一并核实）。
    - `estimate_planar` 的模型凸性检查（4 个叉积）：模型点固定为 80×80 方框（用户填在 YAML 里的 3D 模型），属边界性“防用户”检查，可删。
    - `TRACK_ID_EXHAUSTED`（next_id == UINT64_MAX）：约 2^64 条 track 才触发，实际打不到，删。
    - `measurement_adapter.cpp` 的 `time_augmented_noise` 对 b/q 的 isfinite/>=0 检查：b（时间界，来自 time_uncertainty_s_，现恒为 0）、q（加速度 PSD，来自 profile）均为用户填的配置，不会为负，删。
    - kalman_cv.cpp 的 "R2：" 注释：见 D21。
    - validate_pose_config / validateTagConfig / validate_tracking_config：已在 A1 待删，不重复。

### I. G4 profile R/Q 初值（解堵 G4）

34. **新建一个 profile 文件**（如 `config/tracking_profile.yaml`），写入 R/Q 初估值，再把 tracking.yaml 的 `profile` 字段指向该文件名。注意机制：tracking.yaml 的 `profile: ""` 是**文件路径**，不是数值区；加载器打开同目录下该文件，读 `camera_mse_m2`（R，3×3）、`acceleration_psd_m2ps3`（q）、`initial_velocity_variance_m2ps2` 三项。
35. **R/Q 是纯数学量级推导，不是视频实测**：作业视频没有 ground truth，无法从视频量出真实 R/Q（拿 G2 自己的输出标定 G2 是循环论证；对位置序列二次差分估 Q 会放大噪声）。按以下已知量推导并**明确标注“初估、待标定”**：
    - R：PnP RMSE 中位数 0.36 px + 焦距 2394 px + 典型距离 1–2 m → 横向约 2e-8 m²、纵深约 2.5e-7 m²（1 m 处）。
    - Q：MARK 0.5 m/s 往返运动、换向约 2 m/s²、RMS 加速度约 0.5–1 m/s² → q 约 0.1–1 m²/s³。
    - 卡尔曼对 2–3 倍误差鲁棒；真实标定待板端实测（G-02）。
36. **不加分步骤运行耗时记录**：现在不加（瘦身+搬家后、板子上跑起来再以 profiling 定热点；总耗时 RunSummary.elapsed_s 已有）。

---

## 三、交付物与收尾

1. **构建 + 测试必须通过**：`cmake -S src/tushenghao -B build -DMARK_BUILD_G2=ON -DMARK_BUILD_G4=ON`（改动后这两个开关已删，构建命令相应调整）→ `cmake --build build` → `ctest --test-dir build`。
2. **离线回放结果必须与改动前一致**：用 `data/raw/marker_video.avi`（1676 帧）跑回放，位姿/跟踪输出与 edc9477 的结果逐字节一致（此前标准：1088 ESTIMATED / 588 UNAVAILABLE）。不一致时先查是否改动引入行为变化。
3. **改动报告**：写一份改动报告（放 `src/tushenghao/docs/`，仿照此前 THRESHOLD_GATE_LABEL_AUDIT.md 的格式），逐项说明：改了什么、为什么改、有疑问的项、发现的清单外同类问题/别的问题、R/Q 初估值的推导过程与“初估、待标定”标注。
4. **更新 flow-guide**：`src/tushenghao/docs/flow-guide.md` 按改动后的结构与命名更新（第 1 节的启动/配置流程、第 2 节的 detection 结构、第 3/4 节的 pose/tracking、输出文件与开关说明）。凡与改动有依赖的代码与文档（CMakeLists、include、YAML、flow-guide、测试）全部同步调整好，不留悬空引用。
5. **中文注释**：按 D22，改动触及的函数/模块补上中文注释——写该板块在流程里的职责（输入、输出、用途、数据流向），**不写这轮改了什么**。好注释：“这一步把检测到的角点按屏幕顺序排序，供 PnP 使用”；坏注释：“这里原来用 X 排序，现在改成 Y”。
6. **文件结构收尾检查**：按总则第 5 条，改完后过一遍目录树——无空目录、无孤儿文件、CMakeLists 与 include 与新结构同步。
