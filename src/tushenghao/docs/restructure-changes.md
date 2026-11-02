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

## 当前检测与二维时序模块（执行中）

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

## pose 模块（执行中）

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

验证待本批全套完成登记。

pose 批次机械修复：Tag 测试也使用迁移后的 g2_test_scene.hpp；将同一已有 fixture 目录显式提供给该测试目标，未复制 fixture 或修改任何期望。

pose 批次验收：`build/restructure-pose-retry1/result.json` 全通过：完整构建、27/27、23/23、7/7、G4 四组合及 1676 帧完整公开结果逐字节相同。
