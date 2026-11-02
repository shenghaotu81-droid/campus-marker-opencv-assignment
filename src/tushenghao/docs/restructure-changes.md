# 布局重构执行记录

基线 `d70c1af`，分支 `refactor/lean`。基线确认工作区干净，瘦身代码均已提交。本文按实际模块批次追加，尚未完成的模块不冒称已执行。算法逻辑、数值阈值、空预算、三关 OPEN 与 G4 公开输出保持。

## 裁决与底稿差异

- §2 以瘦后代码为准：F25 之前只记录未来估计／资格分离方向；本次按 §3.5 冻结 G4 公开输出与现行资格语义，不将过期文字当作语义修改授权。
- F65 一次迁移仓内已知 include 路径，保留 namespace、类型和签名，不另造旧路径转发层。各公开入口随所属模块批次移动，值契约先迁移；原 mark 入口暂存只为未执行模块维持可构建中间状态。
- 只在本仓 src/tushenghao 下重排；不创建／拷入外部仓，不删除或挪动任何历史 evidence/ref/history。不存在的 F09/F35/F69 删除项不复活。
- 底稿的 12 个拆分按实际代码核对：F64 事件编码已在既有类型头 inline，F05 正定检查已下沉至低层头，F67 测试公共头已经生成；不为达到过期文件计数复制实现或制造空源文件。后续归所属模块详细登记。

## 类型契约模块（首轮失败记录，后续已修复通过）

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

## 纯数学模块（已完成）

| 原路径／片段 | 新路径 |
|---|---|
| lib/core/observed_geometry_utils.hpp | include/vision/math/observed_geometry.hpp |
| 同头内 positive_definite 定义 | src/math/covariance/covariance_validation.cpp |
| 原 kalman_cv.hpp 的正定声明依赖 | include/vision/math/covariance_validation.hpp |

底稿对应的 kalman_cv.cpp/.hpp 拆分按瘦后代码执行：函数已在旧 observed_geometry_utils 内 inline，本批从其实际唯一实现搬出，不从 Kalman 复制第二份。函数体、舍入、异常和命名空间不变。covariance 子目录是独立的协方差数学检查步骤；配套公开声明放固定 include/vision/math，不建同内容转发头。输入矩阵／输出 bool 是替换契约，其他几何数学仍为既有纯函数。

验证：`build/restructure-math/result.json` 全通过，完整构建、27/27、23/23、7/7、G4 四组合及 1676 帧完整公开输出均与瘦后基线字节相同。正定实现仅搬迁，数学检查与空预算保持。

## 配置模块（已完成）

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

## 观测模块（已完成）

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

## 当前检测与二维时序模块（已完成）

按执行子步骤拆出 observation/parent/validation/completion、原图 observation/fitting/measurement/ordering/validation/semantics/publication，以及 temporal 的 correspondence/stabilization/display/assembly。成对实现／声明同目录；没有按扩展名分组，没有为步骤新建类。

| 原路径 | 新路径 |
|---|---|
| `include/mark/detector.hpp` | `include/vision/detection/detector.hpp` |
| `lib/corners/corner_edge_fit.cpp` | `src/detection/corners/fitting/corner_edge_fit.cpp` |
| `lib/corners/corner_resolver.cpp` | `src/detection/corners/measurement/corner_resolver.cpp` |
| `lib/corners/corner_edge_fit.hpp` | `src/detection/corners/fitting/corner_edge_fit.hpp` |
| `lib/corners/screen_order.hpp` | `src/detection/corners/ordering/screen_order.hpp` |
| `lib/corners/corner_evidence_validation.hpp` | `src/detection/corners/validation/corner_evidence_validation.hpp` |
| `lib/corners/corner_observation.cpp` | `src/detection/corners/observation/corner_observation.cpp` |
| `lib/corners/detection_validator.hpp` | `src/detection/corners/validation/detection_validator.hpp` |
| `lib/corners/detection_validator.cpp` | `src/detection/corners/validation/detection_validator.cpp` |
| `lib/corners/periodic_line_fit.cpp` | `src/detection/corners/fitting/periodic_line_fit.cpp` |
| `lib/corners/periodic_line_fit.hpp` | `src/detection/corners/fitting/periodic_line_fit.hpp` |
| `lib/corners/semantic_resolver.hpp` | `src/detection/corners/semantics/semantic_resolver.hpp` |
| `lib/corners/corner_observation.hpp` | `src/detection/corners/observation/corner_observation.hpp` |
| `lib/corners/semantic_resolver.cpp` | `src/detection/corners/semantics/semantic_resolver.cpp` |
| `lib/corners/screen_order.cpp` | `src/detection/corners/ordering/screen_order.cpp` |
| `lib/corners/corner_resolver.hpp` | `src/detection/corners/measurement/corner_resolver.hpp` |
| `lib/corners/detection_publication.hpp` | `src/detection/corners/publication/detection_publication.hpp` |
| `lib/corners/corner_evidence_validation.cpp` | `src/detection/corners/validation/corner_evidence_validation.cpp` |
| `lib/corners/detection_publication.cpp` | `src/detection/corners/publication/detection_publication.cpp` |
| `lib/geometry/geometry_l_topology.hpp` | `src/detection/geometry/observation/geometry_l_topology.hpp` |
| `lib/geometry/geometry_anchor_evidence.cpp` | `src/detection/geometry/parent/geometry_anchor_evidence.cpp` |
| `lib/geometry/assignment_match_metrics.hpp` | `src/detection/geometry/completion/assignment_match_metrics.hpp` |
| `lib/geometry/geometry_assignment_completion.cpp` | `src/detection/geometry/completion/geometry_assignment_completion.cpp` |
| `lib/geometry/geometry_anchor_evidence.hpp` | `src/detection/geometry/parent/geometry_anchor_evidence.hpp` |
| `lib/geometry/geometry_observation.hpp` | `src/detection/geometry/observation/geometry_observation.hpp` |
| `lib/geometry/geometry_matcher.hpp` | `src/detection/geometry/parent/geometry_matcher.hpp` |
| `lib/geometry/geometry_observation.cpp` | `src/detection/geometry/observation/geometry_observation.cpp` |
| `lib/geometry/geometry_l_topology.cpp` | `src/detection/geometry/observation/geometry_l_topology.cpp` |
| `lib/geometry/geometry_assignment_completion.hpp` | `src/detection/geometry/completion/geometry_assignment_completion.hpp` |
| `lib/geometry/geometry_validation.cpp` | `src/detection/geometry/validation/geometry_validation.cpp` |
| `lib/geometry/geometry_validation.hpp` | `src/detection/geometry/validation/geometry_validation.hpp` |
| `lib/geometry/geometry_matcher.cpp` | `src/detection/geometry/parent/geometry_matcher.cpp` |
| `lib/pipeline/display_history.hpp` | `src/detection/temporal/display/display_history.hpp` |
| `lib/pipeline/display_history.cpp` | `src/detection/temporal/display/display_history.cpp` |
| `lib/pipeline/temporal_stabilizer.cpp` | `src/detection/temporal/stabilization/temporal_stabilizer.cpp` |
| `lib/pipeline/temporal_geometry.cpp` | `src/detection/temporal/correspondence/temporal_geometry.cpp` |
| `lib/pipeline/decode_stage.cpp` | `src/detection/pipeline/decode/decode_stage.cpp` |
| `lib/pipeline/temporal_geometry.hpp` | `src/detection/temporal/correspondence/temporal_geometry.hpp` |
| `lib/pipeline/temporal_stabilizer.hpp` | `src/detection/temporal/stabilization/temporal_stabilizer.hpp` |
| `lib/pipeline/detector.cpp` | `src/detection/pipeline/execution/detector.cpp` |
| `lib/pipeline/frame_sequence.cpp` | `src/detection/pipeline/input/frame_sequence.cpp` |
| `lib/pipeline/decode_stage.hpp` | `src/detection/pipeline/decode/decode_stage.hpp` |
| `lib/pipeline/stabilize_stage.hpp` | `src/detection/temporal/assembly/stabilize_stage.hpp` |
| `lib/pipeline/stabilize_stage.cpp` | `src/detection/temporal/assembly/stabilize_stage.cpp` |
| `lib/pipeline/detector_diagnostics_access.hpp` | `src/detection/pipeline/execution/detector_diagnostics_access.hpp` |
| `lib/pipeline/frame_sequence.hpp` | `src/detection/pipeline/input/frame_sequence.hpp` |
| `lib/preprocess/preprocess.cpp` | `src/detection/preprocess/preprocess.cpp` |
| `lib/preprocess/preprocess.hpp` | `src/detection/preprocess/preprocess.hpp` |
| `tests/assignment_completion_test.cpp` | `tests/detection/geometry/assignment_completion_test.cpp` |
| `tests/block3_contract_regression_test.cpp` | `tests/detection/pipeline/block3_contract_regression_test.cpp` |
| `tests/corner_edge_fit_test.cpp` | `tests/detection/corners/corner_edge_fit_test.cpp` |
| `tests/corner_resolver_test.cpp` | `tests/detection/corners/corner_resolver_test.cpp` |
| `tests/data/l-topology-rounded-L3.txt` | `tests/detection/data/l-topology-rounded-L3.txt` |
| `tests/data/l-topology-short-M1.txt` | `tests/detection/data/l-topology-short-M1.txt` |
| `tests/data/l-topology-video2-L.txt` | `tests/detection/data/l-topology-video2-L.txt` |
| `tests/data/ms-topology-skipped-six.txt` | `tests/detection/data/ms-topology-skipped-six.txt` |
| `tests/data/ms-video638-topology.txt` | `tests/detection/data/ms-video638-topology.txt` |
| `tests/data/rounded_split_boundary.txt` | `tests/detection/data/rounded_split_boundary.txt` |
| `tests/data/video_fragmented_boundary.txt` | `tests/detection/data/video_fragmented_boundary.txt` |
| `tests/detection_validator_test.cpp` | `tests/detection/corners/detection_validator_test.cpp` |
| `tests/detector_contract_test.cpp` | `tests/detection/pipeline/detector_contract_test.cpp` |
| `tests/display_history_test.cpp` | `tests/detection/temporal/display_history_test.cpp` |
| `tests/geometry_anchor_evidence_test.cpp` | `tests/detection/geometry/geometry_anchor_evidence_test.cpp` |
| `tests/geometry_l_topology_test.cpp` | `tests/detection/geometry/geometry_l_topology_test.cpp` |
| `tests/geometry_matcher_test.cpp` | `tests/detection/geometry/geometry_matcher_test.cpp` |
| `tests/geometry_model_test.cpp` | `tests/detection/geometry/geometry_model_test.cpp` |
| `tests/path_a_competition_test.cpp` | `tests/detection/pipeline/path_a_competition_test.cpp` |
| `tests/screen_order_test.cpp` | `tests/detection/corners/screen_order_test.cpp` |
| `tests/semantic_resolver_test.cpp` | `tests/detection/corners/semantic_resolver_test.cpp` |
| `tests/temporal_integration_test.cpp` | `tests/detection/temporal/temporal_integration_test.cpp` |
| `tests/temporal_stabilizer_test.cpp` | `tests/detection/temporal/temporal_stabilizer_test.cpp` |
| `test_support/block3_fixture.hpp` | `tests/support/detection/block3_fixture.hpp` |
| `test_support/temporal_fixture.hpp` | `tests/support/detection/temporal/temporal_fixture.hpp` |
| `test_support/recovery112_reference.hpp` | `tests/support/detection/corners/recovery112_reference.hpp` |
| `tests/support/recovery112_reference.cpp` | `tests/support/detection/corners/recovery112_reference.cpp` |

GeometryStepsOutput 和所有步骤普通函数的参数／返回值不变，13 路表、T 链体原样搬迁。公开 Detector 仅变 include 路径，namespace 与 API 不变。现有内部测试入口 target 保留同名 ALIAS，仅用于已知构建消费者，不创建源码转发层。tests 与步骤镜像，7 个真实轮廓文件原字节搬迁；参考实现保持独立，不并入生产库。

验证：`build/restructure-detection-retry2/result.json` 全通过，完整构建、27/27、23/23、7/7、G4 四组合产物及 1676 帧公开输出与瘦后基线逐字节相同。

检测批次机械修复：移除整树 lib include 后，CLI 共享 parser 的旧 core 路径不再可见；将已有唯一 cli_options.hpp 搬到 include/vision/config/cli_options.hpp 并一次改全部入口包含，不重新暴露 lib。该瘦身新增头不在过期 draft 文件表中，函数体不变。

检测批次第二次机械修复：corner_edge_fit_test 两个固定轮廓使用复合字符串 data/文件名，随 data 目录搬迁后缺文件；只更正两个资源路径，原轮廓字节／断言／预算不变，保留失败日志并重跑全套。

## pose 模块（已完成）

P01…P11 按 MARK／平面求解、独立复核、Tag 后端分目录。namespace 与全部公开签名保持；库只链接 types/config 与既有 AprilTag，pose_app 自己显式链接检测模块，不再让 pose 库反向依赖 detection。原生库版本检查／对象归属／全部候选和精修逻辑不变；测试按 estimation/tag 镜像，真实噪声与预算保持。

| 原路径 | 新路径 |
|---|---|
| `include/mark/pose_estimator.hpp` | `include/vision/pose/pose_estimator.hpp` |
| `include/mark/tag_detector.hpp` | `include/vision/pose/tag_detector.hpp` |
| `lib/pose/pose_geometry.cpp` | `src/pose/validation/pose_geometry.cpp` |
| `lib/pose/pose_geometry.hpp` | `src/pose/validation/pose_geometry.hpp` |
| `lib/pose/pose_estimator.cpp` | `src/pose/estimation/pose_estimator.cpp` |
| `lib/pose/tag_detector.cpp` | `src/pose/tag/tag_detector.cpp` |
| `tests/pose_contract_test.cpp` | `tests/pose/estimation/pose_contract_test.cpp` |
| `tests/tag_detector_test.cpp` | `tests/pose/tag/tag_detector_test.cpp` |
| `tests/support/g2_test_scene.hpp` | `tests/support/pose/g2_test_scene.hpp` |

本批最终验证结果见后面的同名批次验收记录。

pose 批次机械修复：Tag 测试也使用迁移后的 g2_test_scene.hpp；将同一已有 fixture 目录显式提供给该测试目标，未复制 fixture 或修改任何期望。

pose 批次验收：`build/restructure-pose-retry1/result.json` 全通过：完整构建、27/27、23/23、7/7、G4 四组合及 1676 帧完整公开结果逐字节相同。

## tracking 模块（已完成）

K 链按 measurement/filter/lifecycle/io 区分测量适配、数值更新、实例事务与记录协议；成对声明／实现同处。公开 TargetTracker 只变 include 路径；全部输出类型、字段、资格、两点初始化／重建、创新版本与只读预测函数体不变。依赖 types/config/pose/math 单向，测试按同一步骤镜像。

| 原路径 | 新路径 |
|---|---|
| `include/mark/target_tracker.hpp` | `include/vision/tracking/target_tracker.hpp` |
| `lib/tracking/kalman_cv.cpp` | `src/tracking/filter/kalman_cv.cpp` |
| `lib/tracking/kalman_cv.hpp` | `src/tracking/filter/kalman_cv.hpp` |
| `lib/tracking/measurement_adapter.cpp` | `src/tracking/measurement/measurement_adapter.cpp` |
| `lib/tracking/measurement_adapter.hpp` | `src/tracking/measurement/measurement_adapter.hpp` |
| `lib/tracking/target_tracker.cpp` | `src/tracking/lifecycle/target_tracker.cpp` |
| `lib/tracking/tracking_io.cpp` | `src/tracking/io/tracking_io.cpp` |
| `lib/tracking/tracking_io.hpp` | `src/tracking/io/tracking_io.hpp` |
| `tests/tracking_contract_test.cpp` | `tests/tracking/lifecycle/tracking_contract_test.cpp` |
| `tests/tracking_io_test.cpp` | `tests/tracking/io/tracking_io_test.cpp` |

本批最终验证结果见后面的同名批次验收记录。

tracking 批次验收：`build/restructure-tracking/result.json` 全通过：完整构建、27/27、23/23、7/7，G4 四组合全部数值／语义文件与 1676 帧完整公开结果逐字节相同。

## app 模块（已完成）

主程序入口归 tools/apps；应用内部按 offline/rendering/display/export/completion/reporting 拆一层，保持原 run/process_frame/finish 调用顺序、计时边界和输出协议。入口默认路径改为显式 VISION_SOURCE_ROOT，含义仍是同一份现行配置。新目录不再参与文件层数猜测。通用测试 harness 与观测 fixture 各归 tests/support 子目录，app 测试镜像执行职责。

| 原路径 | 新路径 |
|---|---|
| `app/offline_runner.cpp` | `src/app/offline/offline_runner.cpp` |
| `app/offline_runner.hpp` | `src/app/offline/offline_runner.hpp` |
| `app/input_completion.cpp` | `src/app/completion/input_completion.cpp` |
| `app/input_completion.hpp` | `src/app/completion/input_completion.hpp` |
| `app/debug_renderer.cpp` | `src/app/rendering/debug_renderer.cpp` |
| `app/debug_renderer.hpp` | `src/app/rendering/debug_renderer.hpp` |
| `app/display_session.cpp` | `src/app/display/display_session.cpp` |
| `app/display_session.hpp` | `src/app/display/display_session.hpp` |
| `app/video_exporter.cpp` | `src/app/export/video_exporter.cpp` |
| `app/video_exporter.hpp` | `src/app/export/video_exporter.hpp` |
| `app/run_console.cpp` | `src/app/reporting/run_console.cpp` |
| `app/run_console.hpp` | `src/app/reporting/run_console.hpp` |
| `app/main.cpp` | `tools/apps/marker/main.cpp` |
| `app/pose_main.cpp` | `tools/apps/pose/pose_main.cpp` |
| `app/tracking_main.cpp` | `tools/apps/tracking/tracking_main.cpp` |
| `app/display_probe.cpp` | `tools/apps/display/display_probe.cpp` |
| `tests/debug_renderer_test.cpp` | `tests/app/rendering/debug_renderer_test.cpp` |
| `tests/observability_integration_test.cpp` | `tests/app/offline/observability_integration_test.cpp` |
| `tests/manual_validation_check.cpp` | `tests/app/manual/manual_validation_check.cpp` |
| `test_support/assertions.hpp` | `tests/support/common/assertions.hpp` |
| `test_support/case_runner.hpp` | `tests/support/common/case_runner.hpp` |
| `test_support/observability_fixture.hpp` | `tests/support/observability/observability_fixture.hpp` |

本批最终验证结果见后面的同名批次验收记录。

app 批次验收：`build/restructure-app/result.json` 全通过，完整构建、27/27、23/23、7/7、G4 四组合与 1676 帧完整公开输出逐字节相同。默认配置通过显式根指向同一资源，显示／视频／完成状态与公共计时边界均保留。

## synth_ref 模块（已完成）

按来源读取、模型数学、枚举、变换、扰动、场景、栅格、真值、编码、生成事务、独立验证分子目录。model.cpp 拆 model/source_inputs；records.cpp 拆 truth_record/record_codec/generation，原静态编码 helper 留 codec，generation 直接调用已有 record_* 契约。没有新增转发或复制实现。tools/synth_ref 仅保留 CLI 与原冻结 inputs/configs 根；库／测试的 CMake 分离，默认独立 CLI 仍从相同工具根运行。SG 核心来源 manifest 按新树实际源／声明／模板登记，版本仍 1.0.0。

| 原路径 | 新路径 |
|---|---|
| `tools/synth_ref/include/synthetic_gen/model.hpp` | `include/vision/synth_ref/model.hpp` |
| `tools/synth_ref/include/synthetic_gen/verify.hpp` | `include/vision/synth_ref/verify.hpp` |
| `tools/synth_ref/include/synthetic_gen/transforms.hpp` | `include/vision/synth_ref/transforms.hpp` |
| `tools/synth_ref/include/synthetic_gen/perturb.hpp` | `include/vision/synth_ref/perturb.hpp` |
| `tools/synth_ref/include/synthetic_gen/raster.hpp` | `include/vision/synth_ref/raster.hpp` |
| `tools/synth_ref/include/synthetic_gen/records.hpp` | `include/vision/synth_ref/records.hpp` |
| `tools/synth_ref/include/synthetic_gen/cases.hpp` | `include/vision/synth_ref/cases.hpp` |
| `tools/synth_ref/include/synthetic_gen/core.hpp` | `include/vision/synth_ref/core.hpp` |
| `tools/synth_ref/include/synthetic_gen/grid.hpp` | `include/vision/synth_ref/grid.hpp` |
| `tools/synth_ref/include/synthetic_gen/build_info.hpp.in` | `src/synth_ref/build_info.hpp.in` |
| `tools/synth_ref/src/grid.cpp` | `src/synth_ref/enumeration/grid.cpp` |
| `tools/synth_ref/src/transforms.cpp` | `src/synth_ref/geometry/transforms.cpp` |
| `tools/synth_ref/src/perturb.cpp` | `src/synth_ref/perturbation/perturb.cpp` |
| `tools/synth_ref/src/raster.cpp` | `src/synth_ref/raster/raster.cpp` |
| `tools/synth_ref/src/cases.cpp` | `src/synth_ref/scene/cases.cpp` |
| `tools/synth_ref/src/verify.cpp` | `src/synth_ref/verification/verify.cpp` |
| `tools/synth_ref/src/main.cpp` | `tools/synth_ref/main.cpp` |
| `tools/synth_ref/tests/tests.cpp` | `tests/synth_ref/tests.cpp` |

本批最终验证结果见后面的同名批次验收记录。

拆分补充：`tools/synth_ref/src/model.cpp` → `src/synth_ref/model/model.cpp` + `src/synth_ref/inputs/source_inputs.cpp`；`tools/synth_ref/src/records.cpp` → `src/synth_ref/truth/truth_record.cpp` + `src/synth_ref/records/record_codec.cpp` + `src/synth_ref/generation/generation.cpp`。函数体保持；原文件不再编译、不留副本。

synth_ref 批次验收：`build/restructure-synth/result.json` 全通过，完整构建、27/27、23/23、合成 7/7、G4 四组合以及 1676 帧完整公开结果逐字节一致。生成器独立 workflow／篡改／手算用例均保留。

## tools-build 模块（已完成）

工具按审计范围、协议复核、恢复回放、位姿分析、跟踪实验、标定输入／选帧／观测／求解拆子目录。顶层仅全局设置与 add_subdirectory；模块各自拥有源表、依赖与测试。F50 独立夹具显式装配所需模块，删除向上导入工程；F52 由 VISION_BUILD_TOOLS／BUILD_TESTING 管理边界，原默认工具集合保留，原独立夹具与合成可单独构建或用显式 VISION_BUILD_FIXTURE_TOOLS 装配。G2/G4 原开关仍默认关闭。

| 原路径 | 新路径 |
|---|---|
| `tools/audit/geometry_audit.cpp` | `tools/audit/geometry/geometry_audit.cpp` |
| `tools/audit/decode_audit.cpp` | `tools/audit/decode/decode_audit.cpp` |
| `tools/audit/temporal_audit.cpp` | `tools/audit/temporal/temporal_audit.cpp` |
| `tools/validation/final_fixes_verify.cpp` | `tools/validation/records/final_fixes_verify.cpp` |
| `tools/validation/observability_verify.cpp` | `tools/validation/records/observability_verify.cpp` |
| `tools/validation/path_a_verify.cpp` | `tools/validation/records/path_a_verify.cpp` |
| `tools/validation/recovery112_benchmark.cpp` | `tools/validation/recovery/recovery112_benchmark.cpp` |
| `tools/validation/recovery112_synthetic.cpp` | `tools/validation/recovery/recovery112_synthetic.cpp` |
| `tools/validation/recovery112_check.py` | `tools/validation/recovery/recovery112_check.py` |
| `tools/validation/pose_sensitivity.cpp` | `tools/validation/pose/pose_sensitivity.cpp` |
| `tools/validation/tracking_lab.cpp` | `tools/validation/tracking/tracking_lab.cpp` |
| `tools/calibration/calibration_config.cpp` | `tools/calibration/input/calibration_config.cpp` |
| `tools/calibration/calibration_config.hpp` | `tools/calibration/input/calibration_config.hpp` |
| `tools/calibration/calibration_video.cpp` | `tools/calibration/frames/calibration_video.cpp` |
| `tools/calibration/calibration_video.hpp` | `tools/calibration/frames/calibration_video.hpp` |
| `tools/calibration/calibration_detection.cpp` | `tools/calibration/observation/calibration_detection.cpp` |
| `tools/calibration/calibration_detection.hpp` | `tools/calibration/observation/calibration_detection.hpp` |
| `tools/calibration/calibration_result.cpp` | `tools/calibration/results/calibration_result.cpp` |
| `tools/calibration/calibration_result.hpp` | `tools/calibration/results/calibration_result.hpp` |
| `tools/calibration/calibration_main.cpp` | `tools/calibration/apps/calibration_main.cpp` |
| `tools/block3_fixture/diagnostics/parent_diagnose.cpp` | `tools/block3_fixture/diagnostics/parent/parent_diagnose.cpp` |
| `tools/block3_fixture/diagnostics/video_diagnose.cpp` | `tools/block3_fixture/diagnostics/video/video_diagnose.cpp` |
| `tools/block3_fixture/diagnostics/video_corner_measure.cpp` | `tools/block3_fixture/diagnostics/corners/video_corner_measure.cpp` |

本批最终验证结果见后面的同名批次验收记录。

构建拆分：旧顶层、g2.cmake/g4.cmake、夹具／合成 CMake 原型的库／入口／测试规则均分配到实际模块。provenance 函数归 cmake/provenance.cmake，cmake/CMakeLists.txt 最后沿真实目标闭包登记。不将未链接模块纳入当前目标算法身份。

tools-build 批次验收：`build/restructure-tools-build-final/result.json` 全通过，27/27、23/23、7/7、G4 四组合和 1676 帧公开结果逐字节一致。额外边界：仅库（工具／测试均关闭）构建通过；关闭工具、开启 G2/G4 测试 27/27 通过；独立夹具五目标构建通过（`build/restructure-tools-boundaries/`）；开启工具与集成夹具、关闭测试的完整构建也通过（`/tmp/restructure-tools-only-*.log`）。F50/F52 余项已落地。

## 最终文件对照（归并中间迁移路径）

以下为实际执行链归并后的最终位置；只列代码／构建／现行工具说明的实际移动，未列历史原件移动，因为它们没有移动。

| 基线／中间来源 | 当前最终位置 |
|---|---|
| `app/debug_renderer.cpp` | `src/app/rendering/debug_renderer.cpp` |
| `app/debug_renderer.hpp` | `src/app/rendering/debug_renderer.hpp` |
| `app/display_probe.cpp` | `tools/apps/display/display_probe.cpp` |
| `app/display_session.cpp` | `src/app/display/display_session.cpp` |
| `app/display_session.hpp` | `src/app/display/display_session.hpp` |
| `app/input_completion.cpp` | `src/app/completion/input_completion.cpp` |
| `app/input_completion.hpp` | `src/app/completion/input_completion.hpp` |
| `app/main.cpp` | `tools/apps/marker/main.cpp` |
| `app/offline_runner.cpp` | `src/app/offline/offline_runner.cpp` |
| `app/offline_runner.hpp` | `src/app/offline/offline_runner.hpp` |
| `app/pose_main.cpp` | `tools/apps/pose/pose_main.cpp` |
| `app/run_console.cpp` | `src/app/reporting/run_console.cpp` |
| `app/run_console.hpp` | `src/app/reporting/run_console.hpp` |
| `app/tracking_main.cpp` | `tools/apps/tracking/tracking_main.cpp` |
| `app/video_exporter.cpp` | `src/app/export/video_exporter.cpp` |
| `app/video_exporter.hpp` | `src/app/export/video_exporter.hpp` |
| `cmake/g2.cmake` | `src/pose/CMakeLists.txt` |
| `cmake/g4.cmake` | `src/tracking/CMakeLists.txt` |
| `include/mark/detector.hpp` | `include/vision/detection/detector.hpp` |
| `include/mark/detector_config.hpp` | `include/vision/config/detector_config.hpp` |
| `include/mark/detector_types.hpp` | `include/vision/types/detector_types.hpp` |
| `include/mark/pose_config.hpp` | `include/vision/config/pose_config.hpp` |
| `include/mark/pose_estimator.hpp` | `include/vision/pose/pose_estimator.hpp` |
| `include/mark/pose_types.hpp` | `include/vision/types/pose_types.hpp` |
| `include/mark/tag_detector.hpp` | `include/vision/pose/tag_detector.hpp` |
| `include/mark/target_tracker.hpp` | `include/vision/tracking/target_tracker.hpp` |
| `include/mark/tracking_config.hpp` | `include/vision/config/tracking_config.hpp` |
| `include/mark/tracking_types.hpp` | `include/vision/types/tracking_types.hpp` |
| `lib/config/app_config.hpp` | `src/config/application/app_config.hpp` |
| `lib/config/config.cpp` | `src/config/detector/config.cpp` |
| `lib/config/config.hpp` | `src/config/detector/config.hpp` |
| `lib/config/marker_geometry_loader.hpp` | `src/config/model/marker_geometry_loader.hpp` |
| `lib/config/observability_config.hpp` | `src/config/application/observability_config.hpp` |
| `lib/config/pose_config.cpp` | `src/config/pose/pose_config.cpp` |
| `lib/config/tracking_config.cpp` | `src/config/tracking/tracking_config.cpp` |
| `lib/config/value_reader.hpp` | `src/config/reader/value_reader.hpp` |
| `lib/core/cli_options.hpp` | `include/vision/config/cli_options.hpp` |
| `lib/core/config_error.hpp` | `src/config/reader/config_error.hpp` |
| `lib/core/corner_budget.hpp` | `src/config/detector/corner_budget_validation.hpp` |
| `lib/core/diagnostics_types.hpp` | `include/vision/observability/frame_record.hpp` |
| `lib/core/file_fingerprint.hpp` | `include/vision/observability/file_fingerprint.hpp` |
| `lib/core/frame_stamp.hpp` | `include/vision/types/frame_stamp.hpp` |
| `lib/core/geometry_types.hpp` | `include/vision/types/detection/geometry_types.hpp` |
| `lib/core/marker_geometry.cpp` | `src/config/model/marker_geometry.cpp` |
| `lib/core/marker_geometry.hpp` | `include/vision/types/marker_geometry.hpp` |
| `lib/core/observed_geometry_utils.hpp` | `include/vision/math/observed_geometry.hpp` |
| `lib/core/prepared_frame.hpp` | `include/vision/types/detection/prepared_image.hpp` |
| `lib/core/timing.cpp` | `src/observability/timing/timing.cpp` |
| `lib/core/timing.hpp` | `src/observability/timing/timing.hpp` |
| `lib/corners/corner_edge_fit.cpp` | `src/detection/corners/fitting/corner_edge_fit.cpp` |
| `lib/corners/corner_edge_fit.hpp` | `src/detection/corners/fitting/corner_edge_fit.hpp` |
| `lib/corners/corner_evidence_validation.cpp` | `src/detection/corners/validation/corner_evidence_validation.cpp` |
| `lib/corners/corner_evidence_validation.hpp` | `src/detection/corners/validation/corner_evidence_validation.hpp` |
| `lib/corners/corner_observation.cpp` | `src/detection/corners/observation/corner_observation.cpp` |
| `lib/corners/corner_observation.hpp` | `src/detection/corners/observation/corner_observation.hpp` |
| `lib/corners/corner_resolver.cpp` | `src/detection/corners/measurement/corner_resolver.cpp` |
| `lib/corners/corner_resolver.hpp` | `src/detection/corners/measurement/corner_resolver.hpp` |
| `lib/corners/corner_types.hpp` | `include/vision/types/detection/corner_types.hpp` |
| `lib/corners/detection_publication.cpp` | `src/detection/corners/publication/detection_publication.cpp` |
| `lib/corners/detection_publication.hpp` | `src/detection/corners/publication/detection_publication.hpp` |
| `lib/corners/detection_validator.cpp` | `src/detection/corners/validation/detection_validator.cpp` |
| `lib/corners/detection_validator.hpp` | `src/detection/corners/validation/detection_validator.hpp` |
| `lib/corners/periodic_line_fit.cpp` | `src/detection/corners/fitting/periodic_line_fit.cpp` |
| `lib/corners/periodic_line_fit.hpp` | `src/detection/corners/fitting/periodic_line_fit.hpp` |
| `lib/corners/screen_order.cpp` | `src/detection/corners/ordering/screen_order.cpp` |
| `lib/corners/screen_order.hpp` | `src/detection/corners/ordering/screen_order.hpp` |
| `lib/corners/semantic_resolver.cpp` | `src/detection/corners/semantics/semantic_resolver.cpp` |
| `lib/corners/semantic_resolver.hpp` | `src/detection/corners/semantics/semantic_resolver.hpp` |
| `lib/geometry/assignment_match_metrics.hpp` | `src/detection/geometry/completion/assignment_match_metrics.hpp` |
| `lib/geometry/geometry_anchor_evidence.cpp` | `src/detection/geometry/parent/geometry_anchor_evidence.cpp` |
| `lib/geometry/geometry_anchor_evidence.hpp` | `src/detection/geometry/parent/geometry_anchor_evidence.hpp` |
| `lib/geometry/geometry_assignment_completion.cpp` | `src/detection/geometry/completion/geometry_assignment_completion.cpp` |
| `lib/geometry/geometry_assignment_completion.hpp` | `src/detection/geometry/completion/geometry_assignment_completion.hpp` |
| `lib/geometry/geometry_l_topology.cpp` | `src/detection/geometry/observation/geometry_l_topology.cpp` |
| `lib/geometry/geometry_l_topology.hpp` | `src/detection/geometry/observation/geometry_l_topology.hpp` |
| `lib/geometry/geometry_matcher.cpp` | `src/detection/geometry/parent/geometry_matcher.cpp` |
| `lib/geometry/geometry_matcher.hpp` | `src/detection/geometry/parent/geometry_matcher.hpp` |
| `lib/geometry/geometry_observation.cpp` | `src/detection/geometry/observation/geometry_observation.cpp` |
| `lib/geometry/geometry_observation.hpp` | `src/detection/geometry/observation/geometry_observation.hpp` |
| `lib/geometry/geometry_validation.cpp` | `src/detection/geometry/validation/geometry_validation.cpp` |
| `lib/geometry/geometry_validation.hpp` | `src/detection/geometry/validation/geometry_validation.hpp` |
| `lib/pipeline/decode_stage.cpp` | `src/detection/pipeline/decode/decode_stage.cpp` |
| `lib/pipeline/decode_stage.hpp` | `src/detection/pipeline/decode/decode_stage.hpp` |
| `lib/pipeline/detector.cpp` | `src/detection/pipeline/execution/detector.cpp` |
| `lib/pipeline/detector_diagnostics_access.hpp` | `src/detection/pipeline/execution/detector_diagnostics_access.hpp` |
| `lib/pipeline/diagnostics_context.cpp` | `src/observability/context/diagnostics_context.cpp` |
| `lib/pipeline/diagnostics_context.hpp` | `src/observability/context/diagnostics_context.hpp` |
| `lib/pipeline/diagnostics_recorder.cpp` | `src/observability/recording/diagnostics_recorder.cpp` |
| `lib/pipeline/diagnostics_recorder.hpp` | `src/observability/recording/diagnostics_recorder.hpp` |
| `lib/pipeline/display_history.cpp` | `src/detection/temporal/display/display_history.cpp` |
| `lib/pipeline/display_history.hpp` | `src/detection/temporal/display/display_history.hpp` |
| `lib/pipeline/frame_sequence.cpp` | `src/detection/pipeline/input/frame_sequence.cpp` |
| `lib/pipeline/frame_sequence.hpp` | `src/detection/pipeline/input/frame_sequence.hpp` |
| `lib/pipeline/run_report.cpp` | `src/observability/reporting/run_report.cpp` |
| `lib/pipeline/run_report.hpp` | `src/observability/reporting/run_report.hpp` |
| `lib/pipeline/stabilize_stage.cpp` | `src/detection/temporal/assembly/stabilize_stage.cpp` |
| `lib/pipeline/stabilize_stage.hpp` | `src/detection/temporal/assembly/stabilize_stage.hpp` |
| `lib/pipeline/temporal_geometry.cpp` | `src/detection/temporal/correspondence/temporal_geometry.cpp` |
| `lib/pipeline/temporal_geometry.hpp` | `src/detection/temporal/correspondence/temporal_geometry.hpp` |
| `lib/pipeline/temporal_stabilizer.cpp` | `src/detection/temporal/stabilization/temporal_stabilizer.cpp` |
| `lib/pipeline/temporal_stabilizer.hpp` | `src/detection/temporal/stabilization/temporal_stabilizer.hpp` |
| `lib/pipeline/temporal_types.hpp` | `include/vision/types/detection/temporal_types.hpp` |
| `lib/pose/pose_estimator.cpp` | `src/pose/estimation/pose_estimator.cpp` |
| `lib/pose/pose_geometry.cpp` | `src/pose/validation/pose_geometry.cpp` |
| `lib/pose/pose_geometry.hpp` | `src/pose/validation/pose_geometry.hpp` |
| `lib/pose/tag_detector.cpp` | `src/pose/tag/tag_detector.cpp` |
| `lib/preprocess/preprocess.cpp` | `src/detection/preprocess/preprocess.cpp` |
| `lib/preprocess/preprocess.hpp` | `src/detection/preprocess/preprocess.hpp` |
| `lib/tracking/kalman_cv.cpp` | `src/tracking/filter/kalman_cv.cpp` |
| `lib/tracking/kalman_cv.hpp` | `src/tracking/filter/kalman_cv.hpp` |
| `lib/tracking/measurement_adapter.cpp` | `src/tracking/measurement/measurement_adapter.cpp` |
| `lib/tracking/measurement_adapter.hpp` | `src/tracking/measurement/measurement_adapter.hpp` |
| `lib/tracking/target_tracker.cpp` | `src/tracking/lifecycle/target_tracker.cpp` |
| `lib/tracking/tracking_io.cpp` | `src/tracking/io/tracking_io.cpp` |
| `lib/tracking/tracking_io.hpp` | `src/tracking/io/tracking_io.hpp` |
| `test_support/assertions.hpp` | `tests/support/common/assertions.hpp` |
| `test_support/block3_fixture.hpp` | `tests/support/detection/block3_fixture.hpp` |
| `test_support/case_runner.hpp` | `tests/support/common/case_runner.hpp` |
| `test_support/observability_fixture.hpp` | `tests/support/observability/observability_fixture.hpp` |
| `test_support/recovery112_reference.hpp` | `tests/support/detection/corners/recovery112_reference.hpp` |
| `test_support/temporal_fixture.hpp` | `tests/support/detection/temporal/temporal_fixture.hpp` |
| `tests/assignment_completion_test.cpp` | `tests/detection/geometry/assignment_completion_test.cpp` |
| `tests/block3_contract_regression_test.cpp` | `tests/detection/pipeline/block3_contract_regression_test.cpp` |
| `tests/config_error_test.cpp` | `tests/config/config_error_test.cpp` |
| `tests/config_roundtrip_test.cpp` | `tests/config/config_roundtrip_test.cpp` |
| `tests/corner_edge_fit_test.cpp` | `tests/detection/corners/corner_edge_fit_test.cpp` |
| `tests/corner_resolver_test.cpp` | `tests/detection/corners/corner_resolver_test.cpp` |
| `tests/data/l-topology-rounded-L3.txt` | `tests/detection/data/l-topology-rounded-L3.txt` |
| `tests/data/l-topology-short-M1.txt` | `tests/detection/data/l-topology-short-M1.txt` |
| `tests/data/l-topology-video2-L.txt` | `tests/detection/data/l-topology-video2-L.txt` |
| `tests/data/ms-topology-skipped-six.txt` | `tests/detection/data/ms-topology-skipped-six.txt` |
| `tests/data/ms-video638-topology.txt` | `tests/detection/data/ms-video638-topology.txt` |
| `tests/data/rounded_split_boundary.txt` | `tests/detection/data/rounded_split_boundary.txt` |
| `tests/data/video_fragmented_boundary.txt` | `tests/detection/data/video_fragmented_boundary.txt` |
| `tests/debug_renderer_test.cpp` | `tests/app/rendering/debug_renderer_test.cpp` |
| `tests/detection_validator_test.cpp` | `tests/detection/corners/detection_validator_test.cpp` |
| `tests/detector_contract_test.cpp` | `tests/detection/pipeline/detector_contract_test.cpp` |
| `tests/diagnostics_record_test.cpp` | `tests/observability/diagnostics_record_test.cpp` |
| `tests/display_history_test.cpp` | `tests/detection/temporal/display_history_test.cpp` |
| `tests/geometry_anchor_evidence_test.cpp` | `tests/detection/geometry/geometry_anchor_evidence_test.cpp` |
| `tests/geometry_l_topology_test.cpp` | `tests/detection/geometry/geometry_l_topology_test.cpp` |
| `tests/geometry_matcher_test.cpp` | `tests/detection/geometry/geometry_matcher_test.cpp` |
| `tests/geometry_model_test.cpp` | `tests/detection/geometry/geometry_model_test.cpp` |
| `tests/manual_validation_check.cpp` | `tests/app/manual/manual_validation_check.cpp` |
| `tests/observability_integration_test.cpp` | `tests/app/offline/observability_integration_test.cpp` |
| `tests/path_a_competition_test.cpp` | `tests/detection/pipeline/path_a_competition_test.cpp` |
| `tests/pose_contract_test.cpp` | `tests/pose/estimation/pose_contract_test.cpp` |
| `tests/screen_order_test.cpp` | `tests/detection/corners/screen_order_test.cpp` |
| `tests/semantic_resolver_test.cpp` | `tests/detection/corners/semantic_resolver_test.cpp` |
| `tests/support/g2_test_scene.hpp` | `tests/support/pose/g2_test_scene.hpp` |
| `tests/support/recovery112_reference.cpp` | `tests/support/detection/corners/recovery112_reference.cpp` |
| `tests/tag_detector_test.cpp` | `tests/pose/tag/tag_detector_test.cpp` |
| `tests/temporal_integration_test.cpp` | `tests/detection/temporal/temporal_integration_test.cpp` |
| `tests/temporal_stabilizer_test.cpp` | `tests/detection/temporal/temporal_stabilizer_test.cpp` |
| `tests/timing_test.cpp` | `tests/observability/timing_test.cpp` |
| `tests/tracking_contract_test.cpp` | `tests/tracking/lifecycle/tracking_contract_test.cpp` |
| `tests/tracking_io_test.cpp` | `tests/tracking/io/tracking_io_test.cpp` |
| `tools/INDEX.md` | `docs/tools/INDEX.md` |
| `tools/audit/decode_audit.cpp` | `tools/audit/decode/decode_audit.cpp` |
| `tools/audit/geometry_audit.cpp` | `tools/audit/geometry/geometry_audit.cpp` |
| `tools/audit/temporal_audit.cpp` | `tools/audit/temporal/temporal_audit.cpp` |
| `tools/block3_fixture/diagnostics/parent_diagnose.cpp` | `tools/block3_fixture/diagnostics/parent/parent_diagnose.cpp` |
| `tools/block3_fixture/diagnostics/video_corner_measure.cpp` | `tools/block3_fixture/diagnostics/corners/video_corner_measure.cpp` |
| `tools/block3_fixture/diagnostics/video_diagnose.cpp` | `tools/block3_fixture/diagnostics/video/video_diagnose.cpp` |
| `tools/calibration/README.md` | `docs/tools/calibration/README.md` |
| `tools/calibration/calibration_config.cpp` | `tools/calibration/input/calibration_config.cpp` |
| `tools/calibration/calibration_config.hpp` | `tools/calibration/input/calibration_config.hpp` |
| `tools/calibration/calibration_detection.cpp` | `tools/calibration/observation/calibration_detection.cpp` |
| `tools/calibration/calibration_detection.hpp` | `tools/calibration/observation/calibration_detection.hpp` |
| `tools/calibration/calibration_main.cpp` | `tools/calibration/apps/calibration_main.cpp` |
| `tools/calibration/calibration_result.cpp` | `tools/calibration/results/calibration_result.cpp` |
| `tools/calibration/calibration_result.hpp` | `tools/calibration/results/calibration_result.hpp` |
| `tools/calibration/calibration_video.cpp` | `tools/calibration/frames/calibration_video.cpp` |
| `tools/calibration/calibration_video.hpp` | `tools/calibration/frames/calibration_video.hpp` |
| `tools/calibration/scripts/frame_picker_README.md` | `docs/tools/calibration/frame_picker.md` |
| `tools/synth_ref/README.md` | `docs/tools/synth_ref/README.md` |
| `tools/synth_ref/docs/fixture_protocol.md` | `docs/tools/synth_ref/fixture_protocol.md` |
| `tools/synth_ref/docs/source_mapping.md` | `docs/tools/synth_ref/source_mapping.md` |
| `tools/synth_ref/include/synthetic_gen/build_info.hpp.in` | `src/synth_ref/build_info.hpp.in` |
| `tools/synth_ref/include/synthetic_gen/cases.hpp` | `include/vision/synth_ref/cases.hpp` |
| `tools/synth_ref/include/synthetic_gen/core.hpp` | `include/vision/synth_ref/core.hpp` |
| `tools/synth_ref/include/synthetic_gen/grid.hpp` | `include/vision/synth_ref/grid.hpp` |
| `tools/synth_ref/include/synthetic_gen/model.hpp` | `include/vision/synth_ref/model.hpp` |
| `tools/synth_ref/include/synthetic_gen/perturb.hpp` | `include/vision/synth_ref/perturb.hpp` |
| `tools/synth_ref/include/synthetic_gen/raster.hpp` | `include/vision/synth_ref/raster.hpp` |
| `tools/synth_ref/include/synthetic_gen/records.hpp` | `include/vision/synth_ref/records.hpp` |
| `tools/synth_ref/include/synthetic_gen/transforms.hpp` | `include/vision/synth_ref/transforms.hpp` |
| `tools/synth_ref/include/synthetic_gen/verify.hpp` | `include/vision/synth_ref/verify.hpp` |
| `tools/synth_ref/src/cases.cpp` | `src/synth_ref/scene/cases.cpp` |
| `tools/synth_ref/src/grid.cpp` | `src/synth_ref/enumeration/grid.cpp` |
| `tools/synth_ref/src/main.cpp` | `tools/synth_ref/main.cpp` |
| `tools/synth_ref/src/model.cpp` | `src/synth_ref/inputs/source_inputs.cpp` |
| `tools/synth_ref/src/perturb.cpp` | `src/synth_ref/perturbation/perturb.cpp` |
| `tools/synth_ref/src/raster.cpp` | `src/synth_ref/raster/raster.cpp` |
| `tools/synth_ref/src/records.cpp` | `src/synth_ref/generation/generation.cpp` |
| `tools/synth_ref/src/transforms.cpp` | `src/synth_ref/geometry/transforms.cpp` |
| `tools/synth_ref/src/verify.cpp` | `src/synth_ref/verification/verify.cpp` |
| `tools/synth_ref/tests/tests.cpp` | `tests/synth_ref/tests.cpp` |
| `tools/validation/final_fixes_verify.cpp` | `tools/validation/records/final_fixes_verify.cpp` |
| `tools/validation/observability_verify.cpp` | `tools/validation/records/observability_verify.cpp` |
| `tools/validation/path_a_verify.cpp` | `tools/validation/records/path_a_verify.cpp` |
| `tools/validation/pose_sensitivity.cpp` | `tools/validation/pose/pose_sensitivity.cpp` |
| `tools/validation/recovery112_benchmark.cpp` | `tools/validation/recovery/recovery112_benchmark.cpp` |
| `tools/validation/recovery112_check.py` | `tools/validation/recovery/recovery112_check.py` |
| `tools/validation/recovery112_synthetic.cpp` | `tools/validation/recovery/recovery112_synthetic.cpp` |
| `tools/validation/tracking_lab.cpp` | `tools/validation/tracking/tracking_lab.cpp` |

## 合并与拆分核对

- 瘦身已完成的身份／重复度量／测试能力合并不重做：F01/F14/F17/F53/F67 的唯一实现保留，不因过期底稿复活重复代码。
- 模型值／加载声明实际拆开；计时／事件编码、记录生命周期／完整 codec、协方差数学／滤波实现、合成来源／模型数学、真值／编码／生成事务均已在实际来源处拆开。
- 顶层 CMake、旧 g2/g4、合成和夹具 CMake 的库／工具／测试规则分配到各自目录，删除旧总源表和向上导入。底稿“12”是瘦前源文件处置计数，部分片段在瘦身已迁出；本次逐项按现存唯一实现归属执行，没有创建空 frame_events 或重复 covariance 文件凑数。
- 所有 .cpp/.hpp 成对步骤保持同职责目录；公开值与声明按 include/vision 固定协议面放置，不用兼容壳转发源码。独立 reference、手算真值和生产算法继续是不同计算路径。

## 默认构建与边界裁决

原主工程默认应用／审计／标定工具继续构建；原独立合成／夹具仍能从自己的入口构建，也可显式 VISION_BUILD_FIXTURE_TOOLS=ON 集成。这样不把额外 JSON/合成依赖强行加入 detector-only 的关闭工具配置。BUILD_TESTING 控制各模块真实测试；关闭普通工具且启用测试时，仅保留测试实际需要的 reader、标定实现和显示探针。behavior 没有新实现或编译目标。

历史 evidence/ref/history、验收结论、冻结合成 inputs/configs 与生产八份 YAML 保持原字节。lean-report 原文不改；draft 仅加执行状态行。旧 lean-changes 是 4c00708 的历史执行报告，保留其当时文件:行，现行位置以本表与新导读为准。

## 已提交并推送的模块批次

| 提交 | 批次 |
|---|---|
| `1460ea6` | [lean] 迁移类型契约并分离模型值与加载声明 |
| `02d6309` | [lean] 迁移纯数学契约并独立协方差验证实现 |
| `0623c72` | [lean] 按读取校验与资源步骤拆出配置模块及测试 |
| `4fe6fb5` | [lean] 分离观测值契约、记录编码与运行生命周期 |
| `6f3d5cf` | [lean] 按取证与时序步骤迁移检测模块及镜像测试 |
| `acb7fcc` | [lean] 按求解复核与Tag步骤迁移位姿模块 |
| `d8657ae` | [lean] 按测量滤波事务与协议迁移跟踪模块 |
| `4c41b85` | [lean] 按读帧显示导出与收尾步骤迁移应用入口 |
| `c64bddf` | [lean] 分离合成来源真值编码与生成事务布局 |
| `477ca86` | [lean] 完成模块构建边界并消除工具向上导入工程 |

## 每批完整验证索引

以下每批均完整构建，27/27、23/23、7/7、四组合 calibrate/verify/replay 与 1676 帧公开结果字节比较全过。日志在 Git 忽略的 `build/` 下，各批 `commands.jsonl` 保留实际命令与退出码，`result.json` 保留结果；首轮机械失败日志也保留。

| 模块 | 通过日志目录 |
|---|---|
| 类型 | `build/restructure-types-retry1/` |
| 纯数学 | `build/restructure-math/` |
| 配置 | `build/restructure-config-retry1/` |
| 观测 | `build/restructure-observability-retry1/` |
| 检测与时序 | `build/restructure-detection-retry2/` |
| 位姿 | `build/restructure-pose-retry1/` |
| 三维跟踪 | `build/restructure-tracking/` |
| 应用 | `build/restructure-app/` |
| 合成 | `build/restructure-synth/` |
| 工具与构建 | `build/restructure-tools-build-final/` |

## 底稿拆分接缝的实际落点

| 原来源 | 执行结果 |
|---|---|
| 顶层 CMakeLists.txt | 规则分入 src 各模块、tools 各工具组、tests 各模块，顶层无源表。 |
| cmake/g2.cmake | 位姿库／依赖归 src/pose；入口归 tools；两项测试归 tests/pose，旧文件删除。 |
| cmake/g4.cmake | 跟踪库归 src/tracking；入口／lab 归 tools；两项测试归 tests/tracking，旧文件删除。 |
| core/marker_geometry.hpp | 只读模型值归 include/vision/types；加载／校验声明归 config/model。 |
| core/timing.cpp 的事件拆分 | 瘦后事件已 inline 到值头；本次从实际值头搬到 observability/events/frame_events.cpp，timing 仅测时。 |
| pipeline/diagnostics_recorder.cpp | 完整 codec 与生命周期实现分开；同字段／同编码函数。 |
| tracking/kalman_cv.cpp 的纯数学片段 | 瘦后正定实现已在 observed 低层头；从实际唯一来源搬到 math/covariance，滤波函数体不变。 |
| tracking/kalman_cv.hpp 的数学声明 | 私有 filter 声明与公开 covariance_validation 数学契约分开。 |
| tools/block3_fixture/CMakeLists.txt 的工程装配 | 只装配所需模块并链接；根工程源／测试／工具规则不再上递归导入。 |
| tools/synth_ref/CMakeLists.txt | 核心库归 src/synth_ref，CLI 归 tools，7 项测试归 tests/synth_ref。 |
| synth_ref/src/model.cpp | 来源读取／规范批准与模型几何分别归 inputs/source_inputs.cpp 与 model/model.cpp。 |
| synth_ref/src/records.cpp | truth/truth_record.cpp、records/record_codec.cpp、generation/generation.cpp，各自明确产物。 |

这张表覆盖源／声明／构建规则的实际接缝；过期计数不意味着复制已被瘦身搬走的函数。独立步骤接口保留，只有跨文件声明／包含／CMake 机械调整。

## 现行文档与图收尾

导读已按执行顺序连续编号，首次括注旧码；不再按 A/B/C 分组替代调用顺序。工具说明移入 docs/tools，验收／历史说明原位不改。README、G2/G4、数据流、记录协议与导航核对当前入口；数据流旧附录行号不再冒称精确位置，精确定位由新导读逐函数给出。六份 JSON/CSV/DOT 根据当前 241 个源／构建／模板文件、562 条工程内 include 边重生成，未解析项为 0，不收录历史源码快照。

32 个 include/vision 公开头逐个独立编译通过，见 `build/restructure-public-headers.log`。三关仍 OPEN；G4 公开字段／输出、13 路表、T 链逻辑和两类空预算均保持。没有创建 behavior 实现，没有合回 main，没有提交历史大包。

最终完整验证如下；本报告不把历史验收升级为硬件通过结论。

## 最终验收

代码收尾提交 `477ca86`；最终 `build/restructure-final/result.json` 为全通过。完整 Release 构建、全量 **27/27**、默认 **23/23**、合成 **7/7**、四组合 calibrate/verify/replay 以及 **1676 帧完整公开结果逐字节一致**。对照基线是瘦后 `d70c1af`，不是过期 draft 的文件状态。公开结果比较覆盖 raw/tracks/display/diagnostics；墙钟计时与来源代码身份不当作算法数值等价字段。

实际基础命令（仓库根）：

```bash
cmake -S src/tushenghao -B /tmp/campus-lean-build -DCMAKE_BUILD_TYPE=Release -DMARK_BUILD_G2=ON -DMARK_BUILD_G4=ON
cmake --build /tmp/campus-lean-build -j4
ctest --test-dir /tmp/campus-lean-build --output-on-failure
cmake -S src/tushenghao -B /tmp/campus-lean-default -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/campus-lean-default -j4
ctest --test-dir /tmp/campus-lean-default --output-on-failure
cmake -S src/tushenghao/tools/synth_ref -B /tmp/campus-lean-synth -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/campus-lean-synth -j4
ctest --test-dir /tmp/campus-lean-synth --output-on-failure
```

四组合是 camera_relative/local_world × nominal_research/bounded_only。每组的实际标定、留出验证、公开回放命令及退出码见 `build/restructure-final/commands.jsonl`；profile、holdout_errors.csv、全部 holdout 轨迹和 replay tracks.yaml 逐字节一致。完整公开结果与原视频帧数核验见同目录 `public-replay.log`、`public-results.jsonl`，测试／构建见 `*-ctest.log`、`*-build.log`。

额外边界与现行契约：

- 关闭工具及测试：仅库完整构建通过，未查找 OpenSSL/nlohmann_json。
- 关闭普通工具、开启 G2/G4 测试：完整构建、27/27 通过，显示探针等真实测试依赖保留。
- 原独立夹具入口五目标构建通过，不向上导入顶层工程。
- 关闭测试、开启工具及集成夹具／合成：完整构建通过。
- 32 个公开头独立编译，所有 66 个原主链步骤均在新导读首次对照一次，74 个执行展开步骤连续编号，文件:行定位与现行链接核对通过。
- lean-report 原文、draft 除顶部状态外的原文、全部历史 evidence/ref/history、合成冻结输入与八份现行 YAML 相对基线不变。

本次未创建新仓、未合 main、未提交 build/证据大包。模块源表、实际步骤子目录、构建依赖、文档和图均已执行；没有挂起的机械修复或待用户批准的布局问题。
