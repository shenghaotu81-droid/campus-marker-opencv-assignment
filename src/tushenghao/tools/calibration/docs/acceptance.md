# 相机标定验收标准

## 标定板规格
- 来源：2026-10-11 PNX 27 算法组培训群
- 类型：圆点阵（circles grid），不是棋盘格
- 尺寸：7x7 圆点
- 间距：0.03m（圆心距，3cm）
- 检测函数：`cv::findCirclesGrid`（`CALIB_CB_SYMMETRIC_GRID`）

## 输入数据
- 视频：`data/raw/calibration_video.avi`
- 分辨率：1440x1080
- 总帧数：6966（约 70.4 FPS）
- 采集设置：曝光 10000，增益 16.0114（见 `data/camera/capture_settings.yaml`）

## 帧选择要求
- 训练集：20-30 帧
  - 圆点阵完整入镜，49 个圆点全部可见
  - 覆盖画面不同位置（中心、四角、边缘）
  - 角度多样（正对、倾斜、旋转）
  - 距离多样（近、中、远）
  - 清晰无运动模糊
- 验证集：独立 5-10 帧，不参与 `calibrateCamera` 求解
- 训练/验证集不得混用同一帧

## 输出
- 文件：`src/tushenghao/config/calibration.yaml`
- 格式：OpenCV FileStorage YAML（`%YAML:1.0` + `schema_version: 1`）
- 内容：
  ```yaml
  %YAML:1.0
  schema_version: 1
  camera_matrix: !!opencv-matrix
    rows: 3
    cols: 3
    dt: d
    data: [fx, 0, cx, 0, fy, cy, 0, 0, 1]
  distortion_coefficients: !!opencv-matrix
    rows: 1
    cols: 5
    dt: d
    data: [k1, k2, p1, p2, k3]
  image_width: 1440
  image_height: 1080
  ```

## 验收指标
| 指标 | 优秀 | 可用 | 拒绝 |
|---|---|---|---|
| 训练集重投影 RMSE | < 0.5px | 0.5-1.0px | > 1.0px |
| 验证集重投影 RMSE | < 0.5px | 0.5-1.0px | > 1.0px |

## 报告要求（ASSIGNMENT.md 第 3 节）
完成标定后必须在报告中说明：
1. 标定板类型、圆点间距和使用的帧（帧号列表）
2. 相机内参矩阵和畸变系数的来源（本工具 + 视频）
3. 标志物坐标系、相机坐标系和单位
4. 训练/验证集划分方式

## 硬性约束
- 路径必须用 `__FILE__` 推导，禁止写死绝对路径
- 圆点间距 0.03m 为群内确认值，不得修改
- 不做全图去畸变；`solvePnP` 阶段直接传原 K + dist
- 命名规范：类 PascalCase，函数 snake_case，成员变量尾缀 `_`
- 中文注释
