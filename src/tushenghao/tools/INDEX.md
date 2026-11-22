# 工具索引

## 现行工具

从仓库根目录构建。应用与标定工具受 `VISION_BUILD_TOOLS` 控制，回放工具受 `BUILD_TESTING` 控制。

| 目标 | 入口 |
|---|---|
| marker_app | `build/marker_app --video src/tushenghao/docs/homework-setup/data/raw/marker_video.avi` |
| calibration_tool | `build/calibration_tool --config src/tushenghao/tools/calibration/configs/calibration_input.yaml` |
| detection_replay、frozen_fit_replay | 读取录像或固定边拟合样本。 |
| ippe_solution_count | 用当前检测链统计录像中的 IPPE 候选。 |
| tracking_replay | `build/tracking_replay observations.yaml src/tushenghao/config/tracking.yaml tracks.yaml` |

详见[相机标定](calibration/README.md)和[标定视频选帧](calibration/frame_picker.md)。
