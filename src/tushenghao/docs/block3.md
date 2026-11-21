# Block3当前状态与验收记录（WIP）

> 历史阶段记录，旧工具和目录按 `08c5c44` 与 `004a671` 阅读。现行说明见[流程导读](flow-guide.md)。

当前算法基准为[冻结v7](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block3/frozen_detector_v7.yaml)。C/H阶段及隔离均720/720，H门槛PASS；全视频1676帧中864帧产生阶段Detection。Block4/5 已合入，公共 `Detector::process()` 在预算完整时返回 DETECTED/NOT_DETECTED；人工 V/Q 等剩余验收仍未完成。

本页只描述现行实现。早期M1最近白块fallback、6/7顶点L分类、P1投影成点、删拟合以及“帧1200出框即完成”均不是当前方案。完整过程与已撤回计划保存在[历史记录](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/history/block3-before-final-organization.md)，各版结果见[证据索引](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block3/INDEX.md)。

## 现行模块与路径

| 责任 | 当前文件/目录 |
|---|---|
| 公开接口与类型 | [detector.hpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/08c5c44/src/tushenghao/include/mark/detector.hpp)、[detector_types.hpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/08c5c44/src/tushenghao/include/mark/detector_types.hpp)、[detector_config.hpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/08c5c44/src/tushenghao/include/mark/detector_config.hpp) |
| 输入、原图视图与共享模型 | [preprocess.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/08c5c44/src/tushenghao/lib/preprocess/preprocess.cpp)、[prepared_frame.hpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/08c5c44/src/tushenghao/lib/core/prepared_frame.hpp)、[marker_geometry.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/08c5c44/src/tushenghao/lib/core/marker_geometry.cpp) |
| L多候选观察与全实测凹点父生成 | [geometry_observation.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/08c5c44/src/tushenghao/lib/geometry/geometry_observation.cpp)、[geometry_l_topology.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/08c5c44/src/tushenghao/lib/geometry/geometry_l_topology.cpp)、[geometry_matcher.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/08c5c44/src/tushenghao/lib/geometry/geometry_matcher.cpp) |
| 原三L验证与M/S补全 | [geometry_validation.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/08c5c44/src/tushenghao/lib/geometry/geometry_validation.cpp)、[geometry_assignment_completion.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/08c5c44/src/tushenghao/lib/geometry/geometry_assignment_completion.cpp)、[共享度量](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/08c5c44/src/tushenghao/lib/geometry/assignment_match_metrics.hpp) |
| 原图连续轮廓、边拟合与证据复核 | [corner_observation.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/08c5c44/src/tushenghao/lib/corners/corner_observation.cpp)、[corner_edge_fit.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/08c5c44/src/tushenghao/lib/corners/corner_edge_fit.cpp)、[corner_evidence_validation.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/08c5c44/src/tushenghao/lib/corners/corner_evidence_validation.cpp) |
| 四角调度、排序、语义、最终验证 | [corner_resolver.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/08c5c44/src/tushenghao/lib/corners/corner_resolver.cpp)、[screen_order.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/08c5c44/src/tushenghao/lib/corners/screen_order.cpp)、[semantic_resolver.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/08c5c44/src/tushenghao/lib/corners/semantic_resolver.cpp)、[detection_validator.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/08c5c44/src/tushenghao/lib/corners/detection_validator.cpp) |
| 单一阶段入口及公共生命周期 | [decode_stage.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/08c5c44/src/tushenghao/lib/pipeline/decode_stage.cpp)、[detector.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/08c5c44/src/tushenghao/lib/pipeline/detector.cpp) |
| 配置和预算 | [config.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/08c5c44/src/tushenghao/lib/config/config.cpp)、[corner_budget.hpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/08c5c44/src/tushenghao/lib/core/corner_budget.hpp)、[运行配置](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/config/detector.yaml) |
| 应用、审计与测试支持 | [app/main.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/08c5c44/src/tushenghao/app/main.cpp)、[audit/decode_audit.cpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/08c5c44/src/tushenghao/tools/audit/decode_audit.cpp)、[test_support/block3_fixture.hpp](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/08c5c44/src/tushenghao/test_support/block3_fixture.hpp) |

流水线：预处理→L多候选/全凹点父生成→原三L验证→M/S补全→原图四角取证→屏幕排序/证据校验→语义归并→阶段Detection。正式模块不读工具侧实例mask和真值。

## 已批准规则

模型坐标与M160/L416/S64已按图纸核对。三L模型/三点仿射及原验证残差5工作px、面积比0.5–2保持；观察按固定六比例epsilon构造真实L多候选，候选及原简化轮廓的实测凹点全部枚举。锚点尝试（包括无效/镜像）计入原1000上限，保留截断与竞争语义。

角点由当前原图连续支持求交，不使用观测顶点直接替代四角，不补预测点、不缓存历史角。工作→原图采用像素中心映射，最终边界校验用原图size。M/S完整补全独立验证全边界、拓扑、面积与方向；缺失/错误身份不再走最近白块fallback。

角点弧距上限4.131595359474918原图px来自用户批准的713组实拍逐角P99+1，合成历史1.5px并列保留；真值定位预算仍为2原图px。其他数值及审批过程见[block3_budget.md](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/block3_budget.md)，没有因H/未标注视频检出数重定预算。

## 验证结果

| 项目 | 当前结果 |
|---|---|
| C阶段/隔离 | 720/720，错误有效输出0 |
| H阶段/隔离 | 720/720，PASS；无噪声144/144、噪声576/576 |
| H非回退 | 原689及前705通过ID全部保留 |
| H独立检查 | 错角、错方向、排序、bbox、证据差异均0；最大真值误差1.3462912原图px |
| 全视频 | 1676/1676条，864帧有Detection，截断0 |
| 视频未输出812帧 | 325零白像素、239不足三L、24无完整补全、106角点取证拒绝、118竞争未排除 |
| 公共接口 | Block4 稳定层已接入；缺批准预算仍 NOT_READY，当前 empty 无 track |
| CTest | 16项；程序数不等于完整§9.1覆盖，旧assert测试的Release限制保留 |

原三L有效父从771增至1112帧，旧771全部保留。相对v4的591检测帧，保留581、新增283，10帧因新增未排除竞争而保守拒绝。不得通过删竞争分支增加检出数量；视频未标注，不把这些差异称为准确率/召回率改善。

完整[修复报告](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block3/05-L-observation-v5-v7/07-L-observation-repair.md)、[H/video分层](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block3/05-L-observation-v5-v7/H-video-v7.md)、[逐帧归因](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block3/05-L-observation-v5-v7/video-v7-all-frames.csv)、[H非回退](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/evidence/block3/05-L-observation-v5-v7/L-observation-H-nonregression.json)保留。源码目录重组后C720、H720、视频1676条除代码摘要/耗时外全部逐条相等，见[目录迁移验证](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/architecture/layout-reorganization-verification.json)。

## D01–D25 当前处理

| 编号 | 当前处理及剩余范围 |
|---|---|
| D01 | 最近白块fallback已删除；assignment严格来源ID/一对一 |
| D02–D05 | 原图重新分割、完整连续弧、指定边联合约束、去重支持点和证据复核已实现 |
| D06–D09 | 原图关联/延伸/连接预算已批准，像素中心映射及原图边界校验已实现 |
| D10–D11 | 方向唯一性消费截断/竞争；明显不完整分支保守拒绝 |
| D12–D15 | 来源/拟合/交点复算、稳定证据ID平局、全局排序能量及有限性检查已实现 |
| D16–D20 | 公共NOT_READY门控、当前四点bbox、唯一阶段入口、输入/时间/reset及负例检查已实现 |
| D21–D22 | 配置校验/往返及M/S窄补全已实现，资源128候选/4096扩展/64输出已批准 |
| D23 | 用户已批准并修复三L观察/凹点枚举；保留原模型/仿射和验证预算，H门槛闭合 |
| D24 | 模型确认、合成统计、关联/资源预算及实拍P99+1均已批准，依据可追溯 |
| D25 | 无GTest依赖；角边绑定仍为内部显式表，绑定配置化待完成 |

这些结论来自现有有限验证；不是“完整§9.1及所有成像条件均通过”。

## 适用边界与未完成项

白色分割严格gray>200，201只是必要像素下限。325暗帧区间为60–108、441–450、870–953、1247–1375、1594–1646（含两端）；原图/工作图零白像素，不改亮度规则，**未标注不判漏检**。详见[适用边界与已知局限](evidence/block3/05-L-observation-v5-v7/适用边界与已知局限.md)。

- V按用户决定暂跳；N/U/O无确认标签，Q无人工真值，视频召回/误检率及人工定位误差未验证。
- 原32负例子集与封存baseline未提供；完整§9.1、模型绑定配置化、一般仿射/透视/更小目标范围未闭合。
- 稳定层/track 已按 Block4 批准范围实现并验收；持久物理身份、空间位姿、marker_code、confidence及硬实时不在当前保证范围。

目录/文档/工具整理不改变上述算法、预算及验收口径。冻结YAML和[ref/](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/tree/004a671/src/tushenghao/docs/ref)保持原位；最新整理与构建结果见[整理验证](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/architecture/docs-tools-organization.md)。本次工作未commit，留用户review。
