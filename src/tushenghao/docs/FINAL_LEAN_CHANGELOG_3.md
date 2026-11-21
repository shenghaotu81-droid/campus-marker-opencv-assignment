# 视觉管线最终精简第三轮修改说明

基线为 `fa6d394`。本轮按更新后的裁决执行 25 项同类问题及 59 项注释建议，其中 19 项删除、2 项调整、4 项保留。迁出的长说明随源码交付。[API 与推导](final/api-derivations.md)、[调参与标定](final/tuning-calibration.md)。

## 按文件记录

| 文件 | 裁决及处理 |
|---|---|
| `include/vision/types/prepared_frame_types.hpp`、`src/detection/preprocess/preprocess.cpp` | 同-1/2/6，删除预处理帧号、时间戳和原图到工作图的无调用接口，保留反向换算，去掉重复包含。建-7/8/11/12，说明配置尺寸、半像素和轴对齐矩形。resize、原图浅视图与换算推导移入 API 文档。 |
| `src/detection/pipeline/detector.cpp`、`include/vision/detection/detector.hpp`、`include/vision/types/detector_types.hpp` | 同-3/4/5/6，内联帧序校验，删除解码后的不可达 reset 及重复初始化。非法输入仍清空帧序，回显的帧号和时间戳使用默认 0。合法解码由公开入口补帧身份。建-3/9，修正结果和 Impl 说明，pimpl 与对象所有权归入 API 文档。 |
| `src/detection/pipeline/decode_stage.cpp`、`src/detection/corners/screen_order.cpp`、`include/vision/types/corner_types.hpp` | 同-6，去掉解码重复包含。建-4/10/11，说明角序由具体接口约定，明确 float 舍入、升回 double 复核、写回 float。排序与安装映射原理归入 API 文档。 |
| `include/vision/config/detector_config.hpp`、`src/detection/geometry/geometry_observation.cpp`、`src/detection/corners/corner_observation.cpp` | 同-6，去掉粗观察、原图观察两处重复包含。建-7/11/12/15/16，区分默认值与配置、外接矩形的开边界、逐点轮廓和触边风险。阈值、轮廓模式、最近点距离迁入 API 文档。 |
| `src/detection/geometry/contour_topology.hpp`、`src/detection/geometry/match_quad.cpp`、`src/detection/corners/corner_edge_fit.cpp` | 同-7，简化顶点查下标时不再检查未找到。建-13/14/17–19，说明叉积与面积符号、六边环下标、索引窗与弧长、探针分组、闭式 L2 和分母保护。轮廓拓扑与求交推导迁入 API 文档。 |
| `include/vision/config/pose_config.hpp`、`include/vision/types/pose_types.hpp`、`include/vision/pose/pose_estimator.hpp`、`src/pose/estimation/pose_estimator.cpp` | 同-8，仅删模型 z=0 的重复判断，保留平面内坐标有限性。按更新后的同-10，保留现有 valid_quad 和调用路径。建-5/6/20–23，说明离线时间、标定契约、至多两个候选、实际 Mat 形状及分歧式。投影、IPPE、分歧二阶矩和似然备选迁入 API 文档。 |
| `src/pose/validation/pose_geometry.cpp`、`src/pose/tag/tag_detector.cpp`、`src/pose/io/pose_io.cpp` | 同-9/11，删灰度 stride 的不可触发检查，记录 decimate=1 时 refine_edges 被忽略。建-24–27，标明累计深度、Tag 返回集合、通用 PLANAR 来源及 optional 条件声明。重投影、C 后端所有权和 YAML 写法迁入 API 文档。 |
| `include/vision/types/tracking_types.hpp`、`src/tracking/lifecycle/target_tracker.cpp` | 同-12/13，删除 ID 字段、递增和发布，去掉确认后的 seed 写入。建-1/2/34–36，说明提交、状态时钟和首量测 seed，合并推进和关联说明。 |
| `src/tracking/filter/kalman_cv.{cpp,hpp}`、`src/tracking/filter/covariance_validation.{cpp,hpp}` | 同-14/15，内联新息所需位置块，明确 predict 的正且有限 dt 契约。同-16 的三道新息检查与整帧事务保留。建-21/28–33，校正初速、NIS、Joseph、正定性与投影方差说明。CV 预测、白加速度积分、Cholesky、NIS 与矩阵语义迁入 API 文档。 |
| `src/tracking/io/tracking_io.cpp`、`src/tracking/measurement/measurement_adapter.cpp`、`tests/validation/replay_compare.py` | 同-12，轨迹流去掉 ID，Python 读端不再要求该字段或输出 ID 数量。C++ tracking_replay 从观测生成轨迹，共用 writer。同-24 的观测读取保留。建-37/38，YAML 示例迁为明确标注的结构示意，量测注释改为配置来源。 |
| `src/app/{offline/offline_runner,export/video_exporter,rendering/debug_renderer,reporting/run_console,reporting/run_report}.cpp` | 同-12，叠加显示从 `track 1` 改为 `track`。建-39–45，说明整次摘要与逐观测计数、未检出、coast、视频发布、青色重投影和输出流。通用 API 放入 API 文档，NIS/Q/R 的工程调参放入标定记录。 |
| `config/tracking_profile.yaml` | 建-46–48，注明基础 R、q 和初速方差的单位及初估条件。方差与 FileStorage 进 API 文档，静态重复性、距离传播、加速工况和独立真值进标定记录。 |
| `tests/{app/offline/offline_chain_test.cpp,pose/estimation/pose_contract_test.cpp,pose/tag/tag_detector_test.cpp,support/detection/block3_fixture.hpp,support/detection/corners/support_pixel_count.hpp,validation/ippe_solution_count.cpp,validation/compare_pose_boundary.py}` | 同-12/22/23/25，轨迹测试核对 ID 缺席，保留旧资格键缺席断言，删空 MARK 夹具的无用字段、无消费者夹具 helper 与统计头。建-49–51，同步合成、Tag、IPPE 和历史比较器说明。 |
| `tools/CMakeLists.txt`、`tools/calibration/CMakeLists.txt`、`tools/calibration/{frames/calibration_video.*,solution/calibration_result.*,observation/calibration_detection.cpp}` | 同-17–19/21，删总帧数、结果帧 ID 列表、重复空集判断及空抽帧早退，标定仅随工具目标构建。同-20 保留板点生成和独立 RMSE helper。建-52–54，注明二维 RMSE、板坐标、空帧表的 CLI 结果和唯一链接入口。 |
| `README.md`、`docs/{INDEX,flow-guide,tools/INDEX,VISION_REFACTOR_INDEX,g4,diagnostics_schema,dataflow-interfaces,lean-changes,block3,block4_acceptance,evidence/g4/INDEX,ref/G4-Codex,validation/pose-status/README}.md` | 建-55–59，标注屏幕发布和物理顺序恢复，区分现行工具、历史命令与冻结版本，给旧源码链接固定提交，为快照范围及缺失附件补说明。索引可进入两份随代码迁移的学习和工程文档。 |
| `docs/FINAL_LEAN_CHANGELOG.md`、`docs/FINAL_LEAN_REPORT_2.md`、`docs/validation/final-lean/tracks.yaml.gz` | 首轮轨迹产物链接固定到 `7f818ec`，第二轮扫查标注基线，现行轨迹期望以新版回放通过 `gzip -n -c` 重新生成。观测归档字节相同。 |

## 构建与回放

- `cmake --build build` 成功，`ctest --test-dir build` **11/11**。
- 新版正式入口回放 `data/raw/marker_video.avi`，共 1676 帧。观测文件与 `fa6d394` 逐字节一致。基线轨迹只移除每条 `track_id` 行后与新版逐字节一致，覆盖 1676 组位置、1675 组速度、1676 个 P 和 1087 个 NIS。状态统计为 1088 MEASURED、588 COASTING，摘要只有运行耗时变化。
- 新版 C++ tracking_replay 从观测流重建的轨迹与正式入口逐字节一致。现行 gzip 归档解压后与该轨迹一致，SHA-256 为 `c4953ba538ea28d381050f660406d25ed4825a241b149145ae4c6ccb3591a187`。
- 叠加轨迹标签去掉数字，保留轨迹状态与位置、速度文字。独立解码对照 1676 帧，像素差异仅落在轨迹文字所在的 MJPG 块。位姿拒绝反例仍由原检查处理。
