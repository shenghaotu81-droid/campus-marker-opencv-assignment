# MARK 视觉链：构建与运行

三 L＋短 M 结构匹配 → 屏幕排序与固定安装映射 → 原图两条 L2 边求角 → 平面 IPPE 名义位姿 → 相机系 CV 跟踪。当前实现与模块职责见[流程导读](docs/flow-guide.md)，改动、取舍及验证见[视觉管线瘦身报告](docs/VISION_LEAN_REPORT.md)。

## 构建

从仓库根目录执行，依赖 C++17、CMake ≥3.16、OpenCV ≥4.6、AprilTag ≥3.3。

```bash
sudo apt-get install --no-install-recommends build-essential cmake libopencv-dev libapriltag-dev
cmake -S src/tushenghao -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

完整管线始终构建。`VISION_BUILD_TOOLS=OFF` 关闭应用和标定工具，`BUILD_TESTING=OFF` 关闭测试；两者同时关闭只构建算法库。各模块公开接口在 `include/vision/`。

## 运行

```bash
build/marker_app --video data/raw/marker_video.avi
```

命令行只接受 `--video VIDEO`。固定读取源树 `config/`：detector.yaml 是检测参数，pose.yaml 是内参引用/四点模型/Tag 配置，tracking.yaml 引用 tracking_profile.yaml；offline.yaml 单独配置输出目录、AVI 文件名和 overlay 开关。

默认输出 `outputs/marker/{observations.yaml,tracks.yaml,summary.yaml,overlay.avi}`。已有目录可复用，同名文件覆盖；更改 `video_filename` 可保留不同叠加视频，文件名使用 `.avi`。`overlay: false` 关闭视频编码与渲染，位姿/跟踪/汇总仍写出。视频固定 MJPG，关闭编码器后从临时 AVI 发布。

配置直接读取手填值，不检查字段类型/范围/schema。终端输入、视频读取、跨模块输入和运行期数值安全检查保留。资源路径相对配置目录解析，输出目录相对运行工作目录解析。

## 验证与边界

1676 帧录像检出 1088 帧、4352 角；位姿 1088 ESTIMATED / 588 UNAVAILABLE。相同模型参数下，四角和位姿与 edc9477 逐字节一致；跟踪仅将协议名改为 `TrackingResult`，其余字节一致。11 项测试通过；叠加视频独立解码验证 1676 帧、1440×1080、源帧周期和 MJPG。

R=diag(2e-8,2e-8,2.5e-7) m²、q=0.5 m²/s³、初速方差=0.25 m²/s² 是**初估、待标定**，不是视频实测；默认得到 1088 MEASURED / 588 COASTING。空 profile 可关闭数值跟踪。录像没有真实位置/速度 ground truth，不能证明板端精度；coast 是预测，相机系速度包含自车运动。

固定安装映射需赛前按实物核验，不支持跨 90° 翻转或上下带交换。详细核验步骤、PnP 多解分歧和输出字段见 flow-guide。历史资料按各报告的基线理解，资料入口见[文档索引](docs/INDEX.md)。
