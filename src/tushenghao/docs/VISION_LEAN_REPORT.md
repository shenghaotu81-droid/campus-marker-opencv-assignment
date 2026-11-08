# 视觉管线瘦身改动报告

日期：2026-11-08。分支：`refactor/vision-lean-fixed`。基线：`edc9477`。

## 1. 原则与交付范围

按指定指令先阅读“一、总原则”，再独立核对清单。手填 YAML 直接转换成配置值；终端误输、图像/跨模块输入、运行期时间与数值安全检查保留。没有调整检测阈值、角点支持常量、候选选择或轨迹状态规则，没有新增运行耗时分步骤记录。

改动包括源码、配置、依赖测试、当前 README/flow-guide，以及标定工具当前使用说明；历史审计/验证证据目录、板端实测数据与板端移植均未改动。指令输入文件保持用户提供的原样。

## 2. 逐项处理表

编号对应指令第二节。表中“保留”是核实后的选择；边界性疑问和协议冲突详见第 3 节。

| 项 | 改动与理由 |
|---|---|
| A1 | 删除 fields、detector/offline/pose/tracking/Tag 配置验证和 CaptureStamp::valid；删除 Tag 验证 cpp/hpp。运行期时间排序/坏时间数据检查留在跟踪入口。 |
| A2 | detector/pose/tracking、四点模型和 calibration 配置不再携带/校验 schema；位姿/跟踪输出 schema 保留，因为它们是文件协议。 |
| A3 | value_reader 只保留 text/integer/number 节点转换；删除 required、错误枚举、数值策略、类型/有限数/范围检查。 |
| A4 | 删除 ConfigError 及头文件；配置 I/O 错误使用 std::runtime_error。 |
| A5 | 实现命名为 src/config/detector/detector_config.cpp；将单使用者 loader 声明并入已有公开 detector_config.hpp，删除内部 config.hpp，不保留转发壳。 |
| A6 | detector 配置、检测实现和检测值类型统一到 mark::detector；加载函数为 load_detector_config，与 pose/tracking 对齐。同步所有调用方。 |
| A7 | G2Config 改为 PoseConfig，包含渲染器、测试场景和应用调用方。 |
| A8 | 删除两个阶段构建开关和 VISION_HAS 条件块，始终构建完整管线；测试统一由 BUILD_TESTING 控制。stamp 始终在同一帧作用域。 |
| A9 | 删除时间戳溢出、目录已存在、位姿/轨迹输出打开、无帧等应用冗余报错；保留 INPUT_OPEN_FAILED 和 INPUT_FPS_INVALID，后者防视频元数据损坏后除零或生成非法 dt。 |
| A10 | AppConfig/OfflineRunConfig 放入 include/vision/config/app_config.hpp，入口与离线运行器无需依赖 src/config 私有目录。 |
| A11 | 删除 tools/common 空壳及 CMake 引用。 |
| B12 | pose.yaml 直接保存四个二维模型点，mm→m 后数值与基线一致；删除 marker_geometry.yaml、模型角绑定类型和多边形查找。测试图案独立收进测试夹具。 |
| B13 | 新增 offline.yaml，输出默认 outputs/marker、overlay.avi、overlay=true；detector.yaml 只含 preprocess/geometry/corner。入口分别加载两份配置。 |
| B14 | 删除 fourcc 参数，固定 MJPG/AVI；视频文件名真正接入 exporter，临时文件使用相同 stem。文件后缀由维护配置者保证。 |
| B15 | overlay 开关在调用方包住 open/write/finish 和渲染；关掉视频仍完整写位姿、轨迹和汇总。没有把开关塞进 exporter。 |
| C16 | marker_app 删除 --config，固定源树 config 目录。标定工具是独立入口，仍使用自己的 --config。 |
| C17 | 删除 --camera、camera: 分支和 decimal_uint64；视频帧号/FPS 生成时间，clock_domain 固定 video-fps。时间戳字段及下游 dt/同帧检查保留。 |
| C18 | CLI 直接检查唯一选项 --video；删除 flags、DuplicateFlag、Fail 模板和错误枚举。未知、缺值、重复、拒绝选项充当值等检查保留。 |
| C19 | 最终命令：marker_app --video VIDEO。 |
| D20 | 阶段代号改为 Detection/Pose/Tracking/下游控制等职责名，测试场景文件和命名空间同步；协议名按用户补充要求改为 TrackingResult。历史基线比较工具中的数据版本含义保留。 |
| D21 | 清理源码/测试/标定工具中的审计编号和历史实现注释；重写为当前职责，修正“创新门槛”等与当前 NIS 排序行为不符的注释。 |
| D22 | 配置装配、检测编排、原图量测、位姿、滤波、量测适配、跟踪生命周期和汇总均补中文职责说明；不写改动经过。 |
| D23 | 自有普通函数使用 snake_case，包括 run_offline、write_run_summary、render_debug_frame、order_screen_corners、fit_observed_corner 等；类型名保留 CamelCase，OpenCV/AprilTag API 不改。 |
| D24 | DecodeStageResult 合入 FrameResult，入口补帧元数据，缺省非法输入语义不变；WhiteComponent 合入 prepared_frame_types.hpp。保留多人使用的 corner_types.hpp 和工作图类型文件；types 直接平铺，统一模块命名空间和成员后缀。 |
| E25 | math 目录解散：正定检查进入 tracking/filter，线段距离进入 detection/contour_topology；保留滤波器四类运行期正定检查，测试 include 同步。 |
| F26 | exporter 删除打开/写入的防御检查、逐帧读回和无消费者的统计 getter/状态成员；只负责 MJPG 编码、release 和改名，验收另行独立解码。 |
| F27 | run_report 迁入 app/reporting，删除 observability 库/目录和 SUMMARY_WRITE_FAILED；核实三处 64 位值字符串写法后保留，原因见第 3 节。 |
| F28 | debug_renderer、run_console 只同步依赖类型、函数名/成员名，渲染与打印逻辑不变。 |
| G29 | detection 在 src/detection 下按 preprocess、geometry、corners、pipeline 保留一级子目录；各步骤内部直接放实现/声明，与 pose/tracking 的目录层级一致。CMake、内部 include、测试步骤 include 同步。 |
| G30 | 帧序实现/声明合入 detector.cpp；角解析和 float 发布合入 decode_stage.cpp，声明收进已有 decode_stage.hpp。测试直接验证这些数学边界，不另造测试钩子。核心拟合/匹配/观察/排序仍独立。 |
| G31 | preprocess 删除格式、尺寸和阈值重复检查，只保存上下文、resize 和比例；非法图像检查留在 Detector 公开入口，配置不在运行期复验。 |
| G32 | 六个轮廓尺度共享命名常量，corner_topology 缓存六个凹凸结果；整理工作图观察排版。计算顺序和数值不变。 |
| H33 | 删除手填模型凸性检查、轨迹 ID 耗尽分支、time_augmented_noise 中 b/q 配置检查，以及滤波中只针对 q/初速配置的检查；保留实际输入/运算结果的有限、深度、时间、正定和溢出保护。 |
| I34 | 新增 tracking_profile.yaml，tracking.yaml 的 profile 指向该相对文件路径；R/q/初速方差分别进入现有数值模型，不增加参数或层次。 |
| I35 | 使用数学量级初估 R=diag(2e-8,2e-8,2.5e-7)、q=0.5、初速方差=0.25；文件与导读明确标“初估、待标定”，推导及纵深假设见第 4 节。 |
| I36 | 不增加分步骤耗时记录，保留运行汇总的 elapsed_s。 |

## 3. 独立核实、疑问与清单外问题

### 3.1 FPS 与时间输入

INPUT_FPS_INVALID 保留。FPS 来自解码器/视频元数据，可能是 0、负数或非有限；它不是手填配置。时间戳直接用 FPS 作除数，跟踪需要合法 dt，不能删掉此检查后制造非法数值。

CaptureStamp::valid 中混杂身份字段必填、像素域与真实时间约束，删除该总开关。跟踪入口保留负时刻、曝光晚于接收/负曝光、非有限/负误差界检查：这些来自实际帧输入，并影响排序和数值运算。上下文变化、重复/倒序、同帧匹配检查继续由各消费者维护，没有新增抽象或资格状态。

### 3.2 整数 YAML 写法

核对本机 OpenCV 4.6 的 core/persistence.hpp：标量 write 重载是 int、float、double、String，没有完整 int64_t/uint64_t 标量写入接口。直接写 64 位 C++ 数值不能等同于“可无损直接写整数”；缩成 int 会丢高位，double 在 2^53 以上不能精确表示所有整数。

因此 run_report、pose_io、tracking_io 对帧号、微秒时刻、下标、轨迹 ID、计数一致保留 std::to_string。已有 tracking_io_test 使用 2^60 的帧号和时间验证无窄化；文件字段类型与基线一致。小范围枚举、schema、布尔仍直接写 int。这是有意的协议选择。

### 3.3 阶段名与协议字节冲突

原指令同时要求清除阶段代号并保持位姿/跟踪文件逐字节一致；跟踪流头的 `G4TrackingResult` 使两者冲突。用户随后明确：改为 `TrackingResult`，与 `PoseObservations` 对称；验收只允许这一处协议名差异。

最终生产写入方与测试回放写入方都使用 `TrackingResult`。比较时仅将基线头部单次出现的 `stream_type: G4TrackingResult` 替换成 `stream_type: TrackingResult`，随后比较整个文件字节。帧内容、状态、数值、序列、字段顺序、缩进和其余元数据不作归一化。

### 3.4 单使用者文件与测试消费者

角解析、float 发布在生产各只有一个使用者，但测试会直接验证复用角片、float 舍入和图像边界。合并 cpp 后把声明收在已有 decode_stage.hpp，既删除独占小文件，又保留独立数学测试。corner_types/prepared_frame_types 是共享类型，继续独立；检测值类型放在 mark::detector，避免新增别名或兼容层。

### 3.5 标定工具同类配置检查

全库扫描发现 calibration_config 同样包含手填 YAML 的 schema、字段、原始整数 token、类型和数值检查；按总原则删除。YAML 只覆盖显式字段，CLI 保留完整 token、未知/重复/缺值、正尺寸、有限正间距检查。视频/图像、圆点观察与求解路径不改变，输出仍保留实际标定参数。当前工具使用说明和输出格式同步去掉 schema 字段，历史标定证据文件不改。

### 3.6 基线中被忽略的标定源码

从 edc9477 执行 git archive 后首次配置失败：tools/calibration/CMakeLists 引用的 results/calibration_result.cpp/.hpp 没有进入 Git；仓库通用 results/ 忽略规则排除了它们，但工作区有现存实现。

处理方式：基线临时源树补入工作区同一对文件，以便构建 marker_app；未改检测/位姿/跟踪基线代码。最终将这对实现移到 tools/calibration/solution/ 并纳入版本控制，CMake/include 同步。此处没有重写标定求解算法，也没有改通用忽略规则。独立干净源树构建用于确认不再依赖隐藏的本地文件。

### 3.7 其它一致性清理

删除 detection_replay 中无人使用的历史 BASELINE 条件分支；测试渲染脚本不再引用已删模型配置。删除无调用者的 exporter 统计/状态接口，清除应用/测试/验证工具对 src/config 私有 include 的无用暴露。已核对目录树和依赖引用，没有空 math/observability/common 目录或 detection 步骤子目录、悬空声明或删除文件的构建引用。

## 4. R/Q 初估推导与限制

这些是数学量级模型，**初估、待标定**，不是从作业视频量得的真实 R/Q。

### 横向位置方差

取像素误差量级 σu≈0.36 px、焦距 f≈2394 px、距离 z≈1 m：

`σx≈z·σu/f≈1.50e-4 m`，方差约 `2.26e-8 m²`，初值取 `2e-8 m²`。两个横向轴相同，先忽略相关项。z=2 m 时该量级随 z² 放大约四倍。

PnP 重投影 RMSE 不能直接当作已标定单角噪声；这里只用来确定量级。离轴姿态、畸变、角点相关性和系统偏差均未由此估计。

### 纵深位置方差的额外假设

80mm 框在 1m、f=2394px 时投影边长约 `s=fL/z=191.5px`。尺度误差传播为：

`σz≈z²·σs/(fL)`。

清单建议的 `Rzz=2.5e-7 m²` 对应 σz=0.5mm，即有效尺度误差 σs≈0.096px（约 0.1px）。**这个尺度误差不能仅由 0.36px RMSE 独立推出**；它包含四角联合拟合及相关性的额外假设。按指令建议量级作为待标定初值写入，但不把它表述为已证实的测量方差。

若直接把 0.36px 当作尺度标准差，会得到约 3.5e-6 m²，明显更大。这是需要真实板端数据核验的模型不确定性；不能以“滤波对 2–3 倍误差鲁棒”为理由保证任意距离/姿态可用。纵深方差近似随 z⁴ 放大，2m 时同假设约 4e-6 m²，当前文件没有距离自适应的新参数。

### 加速度 PSD 与初速方差

运动速度量级约 0.5m/s，换向加速度约 2m/s²，对应速度变化时间量级 0.25–0.5s；RMS 加速度约 0.5–1m/s²。采用相关时间 τ≈0.5s 的粗略等效白噪声模型：

`q≈2τ·σa²≈0.25–1 m²/s³`，取中间量级 `q=0.5 m²/s³`，在建议的 0.1–1 范围内。τ 是推导假设，不新增生产配置项。滤波仍使用连续白加速度积分形式 `q·[dt³/3,dt²/2;dt²/2,dt]`。

首帧没有测得速度，取初速标准差 0.5m/s，即方差 `0.25 m²/s²`；对外仍不发布首帧速度。真实 R/q/初速模型待板端实测标定。作业视频缺真实位置/速度 ground truth，不能用本 PnP 自己的输出循环标定，也不通过位置二次差分放大噪声来声称测得 Q。

## 5. 构建、测试与回放验收

### 构建与测试

```bash
cmake -S src/tushenghao -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

GNU C++ 13.3、OpenCV C++ 4.6、AprilTag 3.3。11/11 测试通过：配置装配/终端负例、拟合、检测输入/时序、排序/float 发布、结构匹配、渲染、完整离线链、平面位姿、Tag、跟踪、64 位时间序列化。

离线链额外核验：自定义 renamed.avi、已有目录复用、从 YAML 加载 overlay=false、关闭叠加后位姿/轨迹文件与开启时逐字节一致。透视真值视频覆盖 24 帧，其中两帧缺观测仍产生 COASTING，后续恢复 MEASURED 和速度。

独立配置 `BUILD_TESTING=OFF,VISION_BUILD_TOOLS=OFF` 的库构建通过；完整干净源树构建和测试也通过，确认已删除文件没有靠旧构建产物或忽略文件补齐。

### 基线与比较方法

工作结果保存在本机 `/tmp/vision-lean-validation/`，不向证据目录写入生成文件。基线源树来自 `git archive edc9477 src/tushenghao`，仅补上第 3.6 节描述的本地标定实现；基线使用原构建开关开启完整管线。

对 `data/raw/marker_video.avi` 做两组对照，视频参数字符串相同以保持 session_id 一致：

1. 原始空 profile：基线完整回放；最终完整 run_offline 通过临时测试驱动使用空 profile、关闭 overlay，比较 observations/tracks。
2. 同一初估 profile：基线 tracking.yaml 指向与最终相同的 R/q/初速文件；最终通过正式 `marker_app --video` 回放，比较 observations/tracks。

生产 CLI 不添加用于验证的 --config。最终无 profile 驱动只调用已有 AppConfig 与 run_offline 接口，不编入生产或提交；跟踪回放工具也交叉检查空 profile 输出。summary 只比计数/原因，elapsed_s 是实际运行时间，不要求字节相同。

| 验收项 | 结果 |
|---|---|
| 总帧数 | 1676 |
| 检出 / 未检出 | 1088 / 588 |
| 真实四角数量 | 4352 |
| 位姿 ESTIMATED / UNAVAILABLE | 1088 / 588 |
| 位姿文件 | 两组均与基线逐字节相同 |
| 检测四角文本 | 与基线逐字节相同 |
| 跟踪文件 | 两组均仅协议名字符串变化，其余逐字节相同 |
| 同 profile MEASURED / COASTING | 1088 / 588 |
| 输入帧接受 / 数值拒绝 | 1676 / 0 |
| overlay | 独立完整解码 1676 帧、1440×1080、源帧周期、MJPG |

SHA-256 与独立解码实测结果：

| 文件/比较 | SHA-256 |
|---|---|
| 四角文本（新旧相同） | `58498e7c7f8a0ed74e8f25495274c77ac3a500140fb1701dbe4f60ce0a679ff2` |
| observations.yaml（两组新旧相同） | `3c20debf9f6c895688d721848baa9fb0ed138906549ccb7d06e03edc706b4a41` |
| 同初估 profile 基线 tracks.yaml | `2d48c43f8bd42c68ed60f9ed9a2f1db7f128d5a5c268929ada497304084f780d` |
| 同初估 profile 最终 tracks.yaml／仅替换协议名的基线 | `457515850169d736e7c8ff22d6fc6f2565b1f9c21e7483bfa6d30822057ac703` |
| 空 profile 基线 tracks.yaml | `bf71b8c0aefeb6074afe71c48033f35a1b2cb83c3468736a660ef268a6fa2adf` |
| 空 profile 最终 tracks.yaml／仅替换协议名的基线 | `13626e644126e0afeccaf9fa0b60ba4e6f4c737b049a8c6e24685e192bc7e263` |

同 profile 的 overlay.avi 也与基线整个文件逐字节相同。独立 Python OpenCV 5.0/FFmpeg 完整解码 1676 帧，尺寸 1440×1080，MJPG；源 FPS=70.408336，AVI 表示为 70.408，与基线 AVI 完全相同，两者四舍五入到微秒的帧周期均为 14203μs。这里验证容器表示精度，没有改帧时间算法或放宽算法阈值。

位姿 RMSE 中位/P95/最大：0.3575726632510804 / 0.6906709466789221 / 0.9263200336298759 px，与基线相同；跟踪事件只有一次 TRACK_CREATED，没有数值拒绝。

## 6. 文件结构与限制

```text
include/vision/
  config/       app、detector、pose、tracking 与 CLI 声明
  detection/    Detector 公开接口
  pose/         位姿与 Tag 公开接口
  tracking/     跟踪公开接口
  types/        五份共享值类型头文件
src/
  config/       application、detector、pose、tracking、reader
  detection/    preprocess、geometry、corners、pipeline（仅一级子目录）
  pose/         estimation、io、tag、validation
  tracking/     filter、measurement、lifecycle、io
  app/          offline、export、rendering、reporting
  types/        值类型 CMake 接口目标
tools/
  apps/marker/
  calibration/  input、frames、observation、solution 等
```

没有引入新的架构层、质量门或运行期可调预算。全长回放证明本视频下的数值/检测行为保持，不证明真实角点、位姿或速度精度；R/Q、安装映射、板端图像与时间仍需实际采集验证。现有板端验证门和数据状态未改。

## 7. Detection 一级目录补充调整

基线：已确认提交 `4455f5e`。按用户对 G29 的补充说明，detection 与 pose/tracking 一样保留一级步骤目录，各步骤内部不再嵌套：

```text
src/detection/
  CMakeLists.txt
  preprocess/   preprocess.cpp/hpp
  geometry/     geometry_observation.cpp/hpp、match_quad.cpp/hpp、contour_topology.hpp
  corners/      corner_edge_fit.cpp/hpp、corner_observation.cpp/hpp、screen_order.cpp
  pipeline/     decode_stage.cpp/hpp、detector.cpp
```

仅移动这 15 个源码/头文件，同步 CMake 源文件路径、生产与测试的 include 路径，以及 flow-guide 和本报告中的当前结构说明。公开头文件、模块命名空间、函数、数据类型、配置、阈值、算法和 `TrackingResult` 协议均不改变。将 include 路径反向归一后，逐个文件与调整前内容相同。

用户提供的 `docs/ref/Codex指令-视觉管线瘦身-通读第1-4节.md` 原文一并纳入提交。该文件记录初始指令，G29 的实际目录解释以本节和更新后的 flow-guide 为准。

验证使用全新构建目录，命令如下：

```bash
cmake -S src/tushenghao -B /tmp/vision-lean-structure-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/vision-lean-structure-build -j4
ctest --test-dir /tmp/vision-lean-structure-build --output-on-failure
```

构建和 11/11 测试通过。重新运行 1676 帧录像的检测坐标回放、正式 marker_app（初估 profile、开启 overlay）及临时完整离线驱动（空 profile、关闭 overlay），与两套留存基线比较：

- 对调整前 `4455f5e`：四角文本、两组 observations.yaml/tracks.yaml、同 profile 的 overlay.avi 全部逐字节相同。
- 对 `edc9477`：四角、位姿和叠加视频逐字节相同；两组跟踪文件只将头部协议名 `G4TrackingResult` 替换为 `TrackingResult`，其余字节相同，未对帧内容作任何归一化。
- 数量保持 1088 检出/4352 角、1088 ESTIMATED/588 UNAVAILABLE、1088 MEASURED/588 COASTING。1676 帧全部接受，事件只有一次 TRACK_CREATED，无数值拒绝。
- 视频独立完整解码为 1676 帧、1440×1080、MJPG、AVI FPS=70.408，与基线相同；源 FPS=70.408336，微秒帧周期同为 14203。summary 除实际运行耗时外字节相同。

第 5 节所列四角、位姿、两组跟踪 SHA-256 全部保持。补充验证日志/输出位于本机 `/tmp/vision-lean-structure-validation/`；证据目录未改。检查确认 detection 根目录只保留 CMakeLists.txt 和四个非空一级子目录，没有更深层目录、转发头或悬空 include。
