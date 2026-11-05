# MARK视觉链：构建与运行

当前实现按三L＋短M完整匹配确定80×80mm白片结构的物理身份，四边形直接给角边，两条角拟合路线，G2保留IPPE多解、绝对预算和充分差距消歧，G4保留NIS与coast。先读[流程导读](docs/flow-guide.md)、[match-only规则](docs/MATCH_ONLY_RULES.md)和[最新G-01/G-02验证报告](docs/G01_G02_VALIDATION_REPORT.md)；旧阶段材料按记录时点理解。

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
| observations.yaml | 启用G2时逐帧全部候选、筛选状态及质量旗 |
| tracks.yaml | 启用G4时逐帧冻结状态；空检测也推进生命周期 |
| overlay.avi | AVI＋MJPG叠加视频，完成后逐帧读回验证再发布 |
| summary.yaml | 帧数、检测／位姿／跟踪计数与可选总耗时 |

没有弹窗，文件输出只读消费主线结果。视频较大，可用Windows系统播放器或`ffplay outputs/marker/overlay.avi`查看；本次已独立解码1676帧，未在Windows GUI实测。

## 当前验证与边界

固定1676帧录像：1088检出、588未检出；1054 Unique、34 Ambiguous、NoSolution=0。G2预算1.10px／1.10px、差距0.25px；相对a582c48全部2176候选数值不变，同一1088帧最佳RMSE中位／P95／最大为0.382429／0.766330／1.037059px。小差距不强选，角质量标签不回变拒绝门。

生产G4 profile仍空，默认NOT_READY。用户授权复用原合成profile的条件回放产生1050 MEASURED／53 COASTING／573 LOST、1011帧速度；相机profile错配和未知时间警告保留，qualified始终false。G-02没有独立速度真值，NIS无法唯一确定Pvv低估倍数。MaixCam、比赛灯光／运动、时间界和速度误差需实测；桌面录像不授予硬件或控制资格。

全部报告、规则与验证资料统一在[src/tushenghao/docs](docs/INDEX.md)，专题入口见[视觉重构索引](docs/VISION_REFACTOR_INDEX.md)。
