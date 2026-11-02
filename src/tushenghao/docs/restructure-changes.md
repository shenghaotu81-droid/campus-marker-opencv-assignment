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
