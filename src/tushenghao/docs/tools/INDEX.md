# 工具索引

## 现行工具

从仓库根目录构建。应用与标定工具受 `VISION_BUILD_TOOLS` 控制，回放工具受 `BUILD_TESTING` 控制。

| 目标 | 入口 |
|---|---|
| marker_app | `build/marker_app --video data/raw/marker_video.avi` |
| calibration_tool | `build/calibration_tool --config src/tushenghao/tools/calibration/configs/calibration_input.yaml` |
| detection_replay、frozen_fit_replay | 读取录像或固定边拟合样本。 |
| ippe_solution_count | 用当前检测链统计录像中的 IPPE 候选。 |
| tracking_replay | `build/tracking_replay observations.yaml src/tushenghao/config/tracking.yaml tracks.yaml` |

## 历史工具

历史工具按以下版本复现。旧路径和命令的详细索引在 [布局整理提交](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/blob/004a671/src/tushenghao/docs/tools/INDEX.md)。

- [Block3 冻结实现](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/tree/08c5c44/src/tushenghao/tools)。
- [Block4 时序审计](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/tree/9b68daa/src/tushenghao/tools)。
- [Block5 可观测性工具](https://github.com/shenghaotu81-droid/campus-marker-opencv-assignment/tree/c7fffe4/src/tushenghao/tools)。
