# 布局重构执行记录

基线 `d70c1af`，分支 `refactor/lean`。基线确认工作区干净，瘦身代码均已提交。本文按实际模块批次追加，尚未完成的模块不冒称已执行。算法逻辑、数值阈值、空预算、三关 OPEN 与 G4 公开输出保持。

## 裁决与底稿差异

- §2 以瘦后代码为准：F25 之前只记录未来估计／资格分离方向；本次按 §3.5 冻结 G4 公开输出与现行资格语义，不将过期文字当作语义修改授权。
- F65 一次迁移仓内已知 include 路径，保留 namespace、类型和签名，不另造旧路径转发层。各公开入口随所属模块批次移动，值契约先迁移；原 mark 入口暂存只为未执行模块维持可构建中间状态。
- 只在本仓 src/tushenghao 下重排；不创建／拷入外部仓，不删除或挪动任何历史 evidence/ref/history。不存在的 F09/F35/F69 删除项不复活。
- 底稿的 12 个拆分按实际代码核对：F64 事件编码已在既有类型头 inline，F05 正定检查已下沉至低层头，F67 测试公共头已经生成；不为达到过期文件计数复制实现或制造空源文件。后续归所属模块详细登记。

## 类型契约模块（构建失败，按 §5 停止）

只读值类型迁入 include/vision/types；marker_geometry 只读值与配置文件加载声明分开。新 src/types/CMakeLists.txt 只定义 INTERFACE 目标，其他实现沿现有目标编译，后续模块批次继续拆除顶层源表。模型加载声明暂置 lib/config，配置模块搬迁时进入最终步骤目录。

| 原路径 | 当前路径 |
|---|---|
| `include/mark/detector_types.hpp` | `include/vision/types/detector_types.hpp` |
| `include/mark/pose_types.hpp` | `include/vision/types/pose_types.hpp` |
| `include/mark/tracking_types.hpp` | `include/vision/types/tracking_types.hpp` |
| `lib/core/frame_stamp.hpp` | `include/vision/types/frame_stamp.hpp` |
| `lib/core/marker_geometry.hpp` | `include/vision/types/marker_geometry.hpp` |
| `lib/core/marker_geometry.hpp` 的加载声明 | `lib/config/marker_geometry_loader.hpp` |

输入／输出／替换点：帧标记、检测／位姿／跟踪值类型与模型身份按值交接；文件读取只通过显式 path 的 loadMarkerGeometry／validateMarkerGeometry 声明消费，不把 IO 声明塞入纯值契约。

验证：合成工具完整构建及 **7/7** 通过。默认／G2+G4 配置通过，但两套完整构建均失败，尚未进入 23/23、27/27、G4 四组合与 1676 帧比较；未提交／未推送。不能据此声称类型模块已验收完成。

停止原因（提示词 §5）：`app/offline_runner.cpp:209` 调用 `loadMarkerGeometry`，原来间接从模型值头取得声明；本批将 IO 声明从值头拆出后，该调用点未显式包含 `config/marker_geometry_loader.hpp`。编译报 `loadMarkerGeometry was not declared in this scope`。这是头文件接缝遗漏，不是算法／语义差异；按“任何一项不过，停下报告”没有继续修复、提交或推送。

实际命令：

```bash
cmake -S src/tushenghao -B /tmp/campus-lean-build -DCMAKE_BUILD_TYPE=Release -DMARK_BUILD_G2=ON -DMARK_BUILD_G4=ON
cmake --build /tmp/campus-lean-build -j4
cmake -S src/tushenghao -B /tmp/campus-lean-default -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/campus-lean-default -j4
cmake -S src/tushenghao/tools/synth_ref -B /tmp/campus-lean-synth -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/campus-lean-synth -j4
ctest --test-dir /tmp/campus-lean-synth --output-on-failure
```

日志保留在 Git 忽略目录 `build/restructure-types/`：`full-build.log`、`default-build.log`、`synth-ctest.log`、`commands.jsonl`、`result.json`。当前分支仍为 `refactor/lean`，HEAD 仍为 `d70c1af`；未完成改动保留在工作区供续做，没有回滚或改历史证据。其余模块、最终连续编号导读与 draft 顶部“已执行”标记尚未实施。

## §5 修正与继续

用户修正：机械性的包含、路径、CMake 失败自行修复并重跑本批全部验证；仅不能修复或修法会改行为／语义／数值／关状态才停止。类型批次已补显式模型加载头到所有直接调用点，原失败日志保留；未修改任何函数体。

### 类型模块修复后验收通过

机械补齐直接调用点的加载声明包含后，`build/restructure-types-retry1/result.json` 全通过：完整构建、全量 27/27、默认 23/23、合成 7/7；G4 四组合 calibrate/verify/replay 及 profile、holdout、公开轨迹均与瘦后基线逐字节相同；1676 帧完整公开结果逐字节相同。原失败记录保留供追溯，本批已修复完成，按最新 §5 继续后续模块。

## 纯数学模块（执行中）

| 原路径／片段 | 新路径 |
|---|---|
| lib/core/observed_geometry_utils.hpp | include/vision/math/observed_geometry.hpp |
| 同头内 positive_definite 定义 | src/math/covariance/covariance_validation.cpp |
| 原 kalman_cv.hpp 的正定声明依赖 | include/vision/math/covariance_validation.hpp |

底稿对应的 kalman_cv.cpp/.hpp 拆分按瘦后代码执行：函数已在旧 observed_geometry_utils 内 inline，本批从其实际唯一实现搬出，不从 Kalman 复制第二份。函数体、舍入、异常和命名空间不变。covariance 子目录是独立的协方差数学检查步骤；配套公开声明放固定 include/vision/math，不建同内容转发头。输入矩阵／输出 bool 是替换契约，其他几何数学仍为既有纯函数。

验证：`build/restructure-math/result.json` 全通过，完整构建、27/27、23/23、7/7、G4 四组合及 1676 帧完整公开输出均与瘦后基线字节相同。正定实现仅搬迁，数学检查与空预算保持。

## 配置模块（执行中）

配置按严格字段读取、检测参数、应用观测参数、模型加载、G2 配置、G4 配置拆成职责子目录；公开配置路径一次迁移到 include/vision/config。默认开关和预算不变。

| 原路径 | 新路径 |
|---|---|
| `include/mark/detector_config.hpp` | `include/vision/config/detector_config.hpp` |
| `include/mark/pose_config.hpp` | `include/vision/config/pose_config.hpp` |
| `include/mark/tracking_config.hpp` | `include/vision/config/tracking_config.hpp` |
| `lib/config/config.cpp` | `src/config/detector/config.cpp` |
| `lib/config/config.hpp` | `src/config/detector/config.hpp` |
| `lib/config/app_config.hpp` | `src/config/application/app_config.hpp` |
| `lib/config/observability_config.hpp` | `src/config/application/observability_config.hpp` |
| `lib/config/value_reader.hpp` | `src/config/reader/value_reader.hpp` |
| `lib/core/config_error.hpp` | `src/config/reader/config_error.hpp` |
| `lib/core/corner_budget.hpp` | `src/config/detector/corner_budget_validation.hpp` |
| `lib/core/marker_geometry.cpp` | `src/config/model/marker_geometry.cpp` |
| `lib/config/marker_geometry_loader.hpp` | `src/config/model/marker_geometry_loader.hpp` |
| `lib/config/pose_config.cpp` | `src/config/pose/pose_config.cpp` |
| `lib/config/tracking_config.cpp` | `src/config/tracking/tracking_config.cpp` |
| `tests/config_error_test.cpp` | `tests/config/config_error_test.cpp` |
| `tests/config_roundtrip_test.cpp` | `tests/config/config_roundtrip_test.cpp` |

附加接缝：原 pose_geometry.cpp 中相同 Tag 参数谓词逐字搬到 src/config/pose/tag_config_validation.cpp；声明单独归对应配置步骤，原名与函数体不变。这样配置模块只链接 types/math，原生 Tag 对象与位姿计算仍归 pose，消除原底稿没有识别的 config→pose 符号反向依赖。配置测试随模块移动并各自登记；独立角点／位姿 oracle 未合并。输入 YAML／类型化配置，输出原配置或原异常；模型 loader 显式路径是单独替换点。

验证：`build/restructure-config-retry1/result.json` 全通过：完整构建、27/27、23/23、7/7、G4 四组合及全部数值／语义产物字节相同、1676 帧公开结果字节相同。

配置批次机械修复：首次全量构建暴露 pose_sensitivity 经旧相对路径使用 pose_geometry 私有声明；将该现有工具显式链接 vision_config_internal_headers。只增加所需配置契约 include 可见性，不公开整树、未改函数逻辑。默认 23/23 与合成 7/7 首轮已过；仍重跑本批全套验证。

## 观测模块（执行中）

recording 的纯 JSON codec 与运行状态／IO 分开；events 的枚举编码与 timing 测时分开；context 只汇集值快照，reporting 输出运行报告，identity 计算字节身份。每类有独立输入输出，未新增框架。

| 原路径 | 新路径 |
|---|---|
| `lib/core/diagnostics_types.hpp` | `include/vision/observability/frame_record.hpp` |
| `lib/core/timing.hpp` | `src/observability/timing/timing.hpp` |
| `lib/core/timing.cpp` | `src/observability/timing/timing.cpp` |
| `lib/core/file_fingerprint.hpp` | `include/vision/observability/file_fingerprint.hpp` |
| `lib/pipeline/diagnostics_context.hpp` | `src/observability/context/diagnostics_context.hpp` |
| `lib/pipeline/diagnostics_context.cpp` | `src/observability/context/diagnostics_context.cpp` |
| `lib/pipeline/diagnostics_recorder.hpp` | `src/observability/recording/diagnostics_recorder.hpp` |
| `lib/pipeline/diagnostics_recorder.cpp` | `src/observability/recording/diagnostics_recorder.cpp` |
| `lib/pipeline/run_report.hpp` | `src/observability/reporting/run_report.hpp` |
| `lib/pipeline/run_report.cpp` | `src/observability/reporting/run_report.cpp` |
| `lib/core/geometry_types.hpp` | `include/vision/types/detection/geometry_types.hpp` |
| `lib/core/prepared_frame.hpp` | `include/vision/types/detection/prepared_image.hpp` |
| `lib/corners/corner_types.hpp` | `include/vision/types/detection/corner_types.hpp` |
| `lib/pipeline/temporal_types.hpp` | `include/vision/types/detection/temporal_types.hpp` |
| `tests/diagnostics_record_test.cpp` | `tests/observability/diagnostics_record_test.cpp` |
| `tests/timing_test.cpp` | `tests/observability/timing_test.cpp` |

必要差异：瘦后 StageDetails 仍 include decode_stage.hpp，虽然渲染已不读 fitter，目录搬迁仍会反向包含检测编排。将现有 PreparedFrame、GeometryBatch、CornerEvidence、TemporalDiagnostics、DecodeStageResult 的值声明搬到 include/vision/types/detection，StageDetails 独立为 include/vision/observability/stage_details.hpp；实例和算法函数仍留原模块，字段／函数体不变。T 的原因到记录枚举转换移到 events/temporal_reason_codec.hpp，步骤不依赖记录器实现。

原 12 拆分对应的事件编码实际来自瘦后的 frame_record 值头，正定实现已在数学批搬出；按实际来源拆，不复制旧 timing／Kalman 已不存在的片段。record_codec.* 拥有原 serializer 的完整匿名 helper 和唯一 serializeFrameRecord；recorder 仅消费声明。file_fingerprint 函数体原样搬入 identity 实现。

验证：`build/restructure-observability-retry1/result.json` 全通过：完整构建、27/27、23/23、7/7、G4 四组合完整产物及 1676 帧公开结果与瘦后基线逐字节相同。

观测批次机械修复：去掉 Context→检测编排的间接包含后，observability_integration_test 的直接 runDecodePipeline 调用缺声明；为各直接调用点补显式 decode_stage.hpp，不让观测契约重新包含算法头。原失败日志保留，重跑本批完整验证。
