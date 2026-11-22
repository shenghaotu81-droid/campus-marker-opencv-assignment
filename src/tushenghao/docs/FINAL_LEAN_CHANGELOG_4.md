# 视觉管线精简收尾修改说明

基线为 `eddb4ea`。

## 源码

| 文件 | 改动 |
|---|---|
| `src/detection/geometry/match_quad.cpp` | 在拓扑调用前说明，简化顶点来自原轮廓，回查时一定能找到。 |
| `src/pose/tag/tag_detector.cpp` | 注释点名 `quad_decimate` 为 1 时忽略 `refine_edges`。 |
| `tools/calibration/frames/calibration_video.{hpp,cpp}` | 删除 `CalibrationVideoInfo.source_path_` 的声明及来自配置的赋值。 |
| `tools/calibration/solution/calibration_result.{hpp,cpp}` | 删除 `CalibrationResult.source_path_` 的声明及从视频信息转抄的赋值。 |

全仓查找确认两成员没有业务消费者。配置加载器中的同名局部变量仍用于从 `VISION_SOURCE_ROOT` 拼接默认标定输出路径。

## 文档

- 从 `src/tushenghao/docs/` 移除 5259 个历史文件，包括旧轮次报告、阶段验收、旧布局与接口说明、冻结指令、历史工具说明、验证日志和回放归档。旧资料仍可从基线提交查阅。
- 保留 [API 与推导](final/api-derivations.md) 和 [调参与标定](final/tuning-calibration.md)，供阅读现行实现和部署校准使用。
- 保留 [作业素材](assets/README.md) 中的相机设置截图与 MARK 图纸。两者用于采集核对、物理角定义和现场安装。
- 将旧 MARK 规则与角点定义中仍适用的内容并入 [流程导读](flow-guide.md)，包括四个物理角的位置、短 M 的取点方式和只对当前图像完整角片做拟合。导读同步现行凸环检查、切向窗单位及 YAML 字段，移除历史验收段。
- 将 [文档索引](INDEX.md)、[工具索引](tools/INDEX.md)、[相机标定说明](tools/calibration/README.md) 和 [选帧说明](tools/calibration/frame_picker.md) 收敛为现行入口。仓库及模块 README 同步导航和目录说明。
- 检查保留 Markdown 的仓库内相对链接及已删除资料的现行引用，链接目标均存在。

## 验证

- `cmake --build build` 成功，`ctest --test-dir build` 全部通过，共 11 项。
- 回放原始 AVI 的全部 1676 帧。与 `eddb4ea` 对照，观测 YAML、轨迹 YAML 和叠加 AVI 逐字节一致。汇总的帧数、状态和原因计数一致。
