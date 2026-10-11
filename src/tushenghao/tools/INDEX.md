# 工具索引

这些是离线生成、校准、验证和诊断工具，复用主检测库；正式检测不读取工具侧真值。源码按用途分类，既有可执行目标名和独立构建入口保留。

```bash
cmake -S src/tushenghao -B build/tushenghao -DCMAKE_BUILD_TYPE=Release -DMARK_COMMIT_LABEL=08c5c44
cmake --build build/tushenghao -j 4
cmake -S src/tushenghao/tools/block3_fixture -B build/block3-fixture -DCMAKE_BUILD_TYPE=Release -DMARK_COMMIT_LABEL=08c5c44
cmake --build build/block3-fixture -j 4
```

## 工具用途

| 类别 | 源码/脚本 | 目标或调用入口 | 用途 |
|---|---|---|---|
| 阶段审计 | [audit/decode_audit.cpp](audit/decode_audit.cpp) | build/tushenghao/decode_audit | 单帧/全视频真实pipeline、证据与状态；不是公共稳定track |
| 时序审计 | [audit/temporal_audit.cpp](audit/temporal_audit.cpp) | temporal_audit | 一次decode的raw/稳定共享装配，独立EXPERIMENTAL语义网格 |
| 时序记录 | [common/temporal_record.hpp](common/temporal_record.hpp) | 无独立入口 | 旧入口薄包装，调用pipeline唯一FrameRecord serializer |
| 共用摘要 | [common/file_digest.hpp](common/file_digest.hpp) | 无独立入口 | 审计/诊断输入和配置SHA |
| 样例生成 | [generation/main.cpp](block3_fixture/generation/main.cpp) | block3_fixture | C/H的plan/sample/generate |
| C校准 | [calibration/measure.cpp](block3_fixture/calibration/measure.cpp) | block3_measure | C固定条件隔离/关联测量，不表示正式H |
| 阶段验证 | [validation/verify.cpp](block3_fixture/validation/verify.cpp) | block3_verify | 默认正式C/H链路；另有明确诊断参数 |
| 资源验证 | [validation/resources.cpp](block3_fixture/validation/resources.cpp) | block3_resources | 固定候选/扩展/截断压力场景 |
| 父诊断 | [diagnostics/parent_diagnose.cpp](block3_fixture/diagnostics/parent_diagnose.cpp) | block3_parent_diagnose | 逐父、类别/锚点及isolated epsilon诊断 |
| 视频阻断诊断 | [diagnostics/video_diagnose.cpp](block3_fixture/diagnostics/video_diagnose.cpp) | block3_video_diagnose | 选帧逐门限诊断，不作为正式验收输出 |
| 实拍角统计 | [diagnostics/video_corner_measure.cpp](block3_fixture/diagnostics/video_corner_measure.cpp) | block3_video_corner_measure | 固定713组逐角原始统计，null不丢分母 |
| 独立结果核查 | [reporting/report_verification.py](block3_fixture/reporting/report_verification.py) | python3 脚本 | 工具侧真值、屏幕顺序、bbox/证据与H门槛核查 |
| C统计报表 | [reporting/summarize.py](block3_fixture/reporting/summarize.py)、[关联报表](block3_fixture/reporting/summarize_association.py) | python3 脚本 | 分层分布与预算测量报表 |
| 角统计报表 | [reporting/summarize_video_corners.py](block3_fixture/reporting/summarize_video_corners.py) | python3 脚本 | 实拍逐角分布及不可测统计 |
| 相机标定 | [calibration/calibration_main.cpp](calibration/calibration_main.cpp) | build/release/calibration_tool | 离线相机标定：7x7 圆点阵求内参 K 和畸变系数，输出 config/calibration.yaml 给 solvePnP 用 |

四个Python脚本共置reporting/，保留对summarize模块的直接导入。旧[synth_ref](synth_ref/README.md)完整保留；其build*/、runs/和__pycache__是参考包历史/生成物，不是现行工具入口，本轮不清理或迁移。历史诊断.cpp留在[证据批次](../docs/evidence/block3/04-four-track-v3-v4/INDEX.md)，不进入新CMake目标。

## 常用命令与输出语义

从仓库根执行，输出路径须使用未存在的新名称：

```bash
build/block3-fixture/block3_fixture plan H build/H-plan-new
build/block3-fixture/block3_fixture sample C build/C-sample-new
build/block3-fixture/block3_measure build/block3-C-v1 build/C-measure-new.jsonl
build/block3-fixture/block3_verify build/block3-H-v1 src/tushenghao/docs/evidence/block3/frozen_detector_v7.yaml build/H-verify-new.jsonl
build/tushenghao/decode_audit data/raw/marker_video.avi all src/tushenghao/docs/evidence/block3/frozen_detector_v7.yaml > build/video-new.jsonl
python3 src/tushenghao/tools/block3_fixture/reporting/report_verification.py build/H-verify-new.jsonl build/video-new.jsonl build/H-video-new.md
build/block3-fixture/block3_resources > build/resources-new.jsonl
```

- block3_verify正式H：退出0为H门槛PASS，2为H未达门槛，1为输入/配置错误；C退出0是诊断回归完成，不冒称独立H。
- verify的 `--diagnose-stage`、`--diagnose-one`、`--candidate-config`是辅助模式，不用它们代替默认正式验证。
- report_verification.py退出0表示报告写入完成，是否通过须读输出JSON中的H_acceptance；无视频标签时recall/Q保持null。
- decode_audit退出0表示解码/请求帧处理完成，Detection数量不等于正确率；公共状态为NOT_EVALUATED，工具未调用process。
- parent/video诊断中的isolated输出保留diagnostic_only，不用于宣传正式成功率。

最新结果入口：[Block3证据索引](../docs/evidence/block3/INDEX.md)。源码目录细节：[README](../README.md)。

## Block4新增工具

```bash
build/block4/temporal_audit --video data/raw/marker_video.avi --config src/tushenghao/config/detector.yaml --output build/block4-evidence/new-video.jsonl
build/block4/temporal_audit --experiment-grid --noise-csv build/block4-evidence/C-fixed-noise.csv --config build/block4-evidence/EXPERIMENTAL-detector.yaml --output build/block4-evidence/new-grid.jsonl
```

视频模式三参数均必填。实验模式用`--experiment-grid`与必填`--noise-csv`替代`--video`，`--config/--output`仍必填；CSV首列为C case_work_id，后八列为物理四角dx/dy，固定顺序至少32行。工具固定32次调用/段、7/14/28ms、100px方框、平移与旋转独立对照、已知/未知方向及有/无扰动、四档偏离，不从视频调值。r必须由独立EXPERIMENTAL配置提供；偏离档0.5/1/2/4仅用于网格，不写生产。原始grid行包含配置/扰动/模型/代码hash，统计方法、支持范围和可重放命令见[Block4预算](../docs/block4_budget.md)。

输出父目录可创建，已有输出或`.effective.yaml`默认拒绝，显式`--overwrite`才覆盖指定产物；输入/配置/记录失败退出1，成功完成退出0。生产缺预算允许记录raw阶段但共享最终结果为NOT_READY、空有效载荷，不把实验配置当批准配置。两模式都不实现Block5计时框架。

## Block5 当前工具接口

[README离线命令](../README.md#block5-离线可观测性)与[schema](../docs/diagnostics_schema.md)是当前入口；上述Block3/4独立stdout JSON或`.effective.yaml`是历史协议。

- geometry_audit：`--video VIDEO --config YAML --run-dir NEW`，完整几何链、geometry scope。
- decode_audit：保留`VIDEO [all|帧列表] [YAML]`，增加`--run-dir NEW`；帧列表是披露的执行子集。
- temporal_audit：`--video`或`--experiment-grid --noise-csv`，`--config`和`--run-dir`；旧`--output`迁到新`.run`目录，不覆盖旧材料。
- observability_verify：`--compare OLD NEW [--route-b]`、`--check-run RUN`、`--check-archive BLOCK5`，均另给`--report NEW.json`；未知/重复/缺输入及已有报告非零。

统一FrameRecord、serializer与统计；没有公共process的工具不能给14ms性能结论。原H fixture复用输入，不重新生成网格，历史工具写盘缺口与本轮验证见[验收](../docs/block5_acceptance.md)；工具退出0也必须核产物。
