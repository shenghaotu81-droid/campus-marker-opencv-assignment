# 相机标定工具（calibration_tool）

离线相机标定工具：用 7x7 圆点阵标定板视频求解相机内参 K 和畸变系数 dist，输出给 `solvePnP` 使用。

## 构建

```bash
cmake -S src/tushenghao -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --target calibration_tool
```

## 使用流程

### 1. 选帧

```bash
python3 src/tushenghao/tools/calibration/scripts/frame_picker.py data/raw/calibration_video.avi
```

按键：`n`/`p`（或左右方向键）±50 帧，`j`/`k` ±1 帧，`s` 标记，`d` 撤销，`q` 退出并打印 `frame_list`。

选帧标准：49 个圆点全可见、清晰无模糊、角度/位置/距离尽量散开。

### 2. 填配置

把 `frame_picker.py` 输出的 `frame_list: [...]` 粘贴到 `tools/calibration/configs/calibration_input.yaml`。

### 3. 运行标定

```bash
./build/release/calibration_tool --config src/tushenghao/tools/calibration/configs/calibration_input.yaml
```

### 4. 看结果

终端输出重投影误差，`src/tushenghao/config/calibration.yaml` 写入标定结果。

## 配置说明

三层优先级：C++ 默认值 → yaml → CLI 参数覆盖。

| 参数 | 说明 |
|---|---|
| `video_path` | 标定视频路径 |
| `board_rows` / `board_cols` | 标定板行列数（7x7） |
| `point_spacing` | 圆心间距，单位米（0.03） |
| `frame_list` | 选定的帧号列表 |
| `output_path` | 输出 yaml 路径（默认 `config/calibration.yaml`） |

CLI 可覆盖：`--video`、`--output`、`--rows`、`--cols`、`--spacing`。

配置步骤先检查完整 CLI，再按默认值 → YAML → CLI 覆盖。未知、重复和缺值选项报错；CLI 数值必须完整解析，尺寸和间距须为正，间距须有限。手填 YAML 直接读取已填写字段，不检查未知/重复键、类型、范围或 schema。文件打开与实际图像/求解失败仍报告错误。

选帧器到末帧显示 `END_OF_INPUT`，仍可回看；按 `q` 以 `USER_FINISH` 正常结束。定位或解码失败返回非零并显示 `READ_ERROR`，保留失败前已选列表，但不把它描述为正常完成的选帧结果。

## 模块结构

```
calibration_main.cpp      # 流程编排
calibration_config.*      # 配置三层覆盖
calibration_video.*       # 视频打开 + 按列表抽帧
calibration_detection.*   # findCirclesGrid 检测 + 生成 3D 点
solution/calibration_result.* # calibrateCamera 求解 + 写 yaml
```

## 输出格式

`config/calibration.yaml`（OpenCV FileStorage）：

```yaml
%YAML:1.0
camera_matrix: !!opencv-matrix      # 3x3 内参
distortion_coefficients: !!opencv-matrix  # 1x5 畸变
image_width: 1440
image_height: 1080
reprojection_error: 0.095         # 重投影误差（像素）
```

## 验收标准

见 `docs/acceptance.md`。重投影 RMSE < 0.5px 为优秀。

选帧使用 `frame_list`；抽样与圆点阵观察保留源 frame_id，未检出帧记信息且不进入求解。视频尺寸由 CalibrationVideoInfo 传到求解与保存步骤。输出相机矩阵、畸变、尺寸和重投影误差。
