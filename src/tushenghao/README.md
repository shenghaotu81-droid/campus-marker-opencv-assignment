# MARK视觉链：构建与运行

本固定安装分支使用显式屏幕→物理对应（配置存P→屏幕槽位），不逐帧猜身份；换装与赛前核验见[固定映射交付](docs/FIXED_MAPPING_REPORT.md)。

当前实现：三L＋短M完整结构匹配→屏幕排序/固定安装映射→单候选四边形引导L2角点→IPPE多解/名义测量→相机系CV跟踪。没有角点候选搜索、周期回退或经验质量资格门。先读[当前收尾报告](docs/POSE_STATUS_FINALIZATION_REPORT.md)、[156项落实表](docs/THRESHOLD_GATE_LABEL_IMPLEMENTATION.md)与[流程导读](docs/flow-guide.md)。旧阶段材料按快照理解。

## 构建

从仓库根目录执行。依赖C++17、CMake≥3.16、OpenCV；启用G2需AprilTag 3开发包。

```bash
sudo apt-get install --no-install-recommends build-essential cmake libopencv-dev libapriltag-dev
cmake -S src/tushenghao -B build -DCMAKE_BUILD_TYPE=Release -DMARK_BUILD_G2=ON -DMARK_BUILD_G4=ON
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

只检测可关闭G2/G4，G4依赖G2。`VISION_BUILD_TOOLS=OFF`可只构建库，`BUILD_TESTING=OFF`不构建测试。检测库不链接G2/G4；各模块公开接口在`include/vision/`。

## 运行

```bash
build/marker_app --video data/raw/marker_video.avi --config src/tushenghao/config/detector.yaml
```

摄像头用`--camera 0`替换`--video ...`。CLI仅接受`--video`／`--camera`二选一及可选`--config`；旧mode、display、run-dir、expected-frames、pose_app和tracking_app入口均已删除。

输出目录由detector YAML的`output.run_directory`指定，未填为`outputs/marker`；已有目录拒绝覆盖。每次运行应配置新目录。资源路径相对配置文件解析；G2/G4从detector YAML同目录读取pose.yaml和tracking.yaml，临时复制配置时要带齐相对依赖。

| 文件 | 内容 |
|---|---|
| observations.yaml | 启用G2时schema6：可选名义估计、RMSE、平移分歧矩阵；ESTIMATED/UNAVAILABLE |
| tracks.yaml | 启用G4时schema2：相机系位置/速度/P、Measured或Coasting；空检测也推进预测 |
| overlay.avi | AVI＋MJPG叠加视频，完成后逐帧读回验证再发布 |
| summary.yaml | 帧数、检测／位姿／跟踪计数与总耗时 |

没有弹窗，文件输出只读消费主线结果。视频较大，可用Windows系统播放器或`ffplay outputs/marker/overlay.avi`查看；本次已独立解码1676帧，未在Windows GUI实测。

## 当前验证与边界

固定1676帧录像：1088检出、588未检出、4352角，与f1ee955检出集合一致；1088 ESTIMATED、588 UNAVAILABLE；名义位姿/RMSE与a392bd4完全一致，求解失败0。最佳PnP RMSE中位/P95/最大为0.357573/0.690671/0.926320px，三项低于基线。

G2内部保留IPPE求解与数学复核，只输出可选名义估计和位置分歧S；G4直接用R+S，不读取候选或唯一性标签。G3只消费G4的位置/速度/P和Measured/Coasting。生产profile为空则跳过G4数值计算；条件回放复用原合成R/q/vv，得到1088 Measured、588 Coasting。coast是预测，不是仍检测到MARK；相机系速度包含自车运动。录像没有独立真实位置/速度真值，MaixCam精度、噪声与时间仍待实拍，详细指标和限制见报告。

配置schema=2；只维护detector.yaml，已删除两个相同副本。位姿文件schema=6、跟踪schema=2；生产不维护旧文件读回或配置写回。回放比较工具和fixture属于tests，模块不依赖测试支撑。

全部报告、规则与验证资料统一在[src/tushenghao/docs](docs/INDEX.md)，专题入口见[视觉重构索引](docs/VISION_REFACTOR_INDEX.md)。
