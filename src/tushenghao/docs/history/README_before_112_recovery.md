# MARK 检测：快速开始

本目录实现基于白色几何结构的 MARK 检测、当前帧角点取证、时序稳定及离线诊断。以下操作适用于 Ubuntu/Linux，均从**仓库根目录**执行；输入视频由你提供。

## 第一步：安装依赖并构建

需要 C++17、CMake ≥3.16、OpenCV 和 OpenSSL。每条命令均为单行：

```bash
sudo apt-get update && sudo apt-get install --no-install-recommends build-essential cmake libopencv-dev libssl-dev
cmake -S src/tushenghao -B build/release -DCMAKE_BUILD_TYPE=Release && cmake --build build/release -j4
```

独立标定工具额外需要 `nlohmann-json3-dev`，见[工具索引](tools/INDEX.md)；普通 App 无须构建这些工具。`clang-format` 仅用于开发排版，不是运行依赖。

## 第二步：检查配置并选择一种运行方式

替换下面的视频占位符；路径含空格时保留双引号。

```bash
VIDEO="<你的视频绝对路径>"
build/release/marker_app --check-config --config src/tushenghao/config/detector_verification.yaml
build/release/marker_app --check-config --config src/tushenghao/config/detector.yaml
build/release/marker_app --check-config --config src/tushenghao/config/detector_debug.yaml
```

三种配置均检查通过后，选择一种方式运行，不必首次全部执行：

| 方式 | 用途 | 输出目录 |
|---|---|---|
| baseline | 无弹窗的处理与汇总 | `new-runs/quickstart-baseline-01` |
| debug + display | 看检测框与逐帧诊断 | `new-runs/quickstart-debug-01` |
| verification + export-video | 保留完整逐帧证据和带框视频 | `new-runs/quickstart-verification-01` |

```bash
build/release/marker_app --video "$VIDEO" --config src/tushenghao/config/detector.yaml --mode baseline --run-dir new-runs/quickstart-baseline-01
build/release/marker_app --video "$VIDEO" --config src/tushenghao/config/detector_debug.yaml --mode debug --display --run-dir new-runs/quickstart-debug-01
build/release/marker_app --video "$VIDEO" --config src/tushenghao/config/detector_verification.yaml --mode debug --run-purpose verification --export-video --run-dir new-runs/quickstart-verification-01
```

输出必须使用新目录。再次运行将对应后缀改成 `02` 等，不覆盖既有证据。`--display` 需要图形桌面；无图形环境时移除该选项或选择视频导出，以文件查看结果。verification 是报告用途，CLI 使用 `--mode debug --run-purpose verification`。

`--expected-frames N` 可在帧数已知时核查截断，不是固定常量。省略时程序使用可用的容器帧数；无法可靠核验时报告“完整性未验证”，不能据此认定完整成功。已知本项目原视频回归才可显式填写 1676。

## 第三步：查看结果

在所选 `--run-dir` 中查看：

| 文件 | 内容 |
|---|---|
| `summary.yaml` | 帧数、处理时间和状态汇总 |
| `frames.jsonl` | debug/verification 的逐帧状态和诊断；baseline 不输出帧详情 |
| `effective_config.yaml`、`manifest.yaml` | 本次配置、输入与运行来源 |
| `overlay.mp4` | 带框视频，仅启用导出时存在 |

终端保留技术 ID；中文摘要帮助快速查看帧数、耗时和瓶颈。先看退出码和运行完整性；异常时查 `FAILED.json` 或 `INCOMPLETE.json`。检出数量不等于正确率。

导出采用现有 MJPG in MP4，部分 VS Code 内置播放器不支持。用系统播放器打开，或安装 FFmpeg 后执行：

```bash
ffplay new-runs/quickstart-verification-01/overlay.mp4
```

## 路径与已知限制

配置模型路径相对 YAML 所在目录解析；命令中的视频和输出路径按当前工作目录解析。库使用者包含 `<mark/detector.hpp>`，内部模块见 `lib/`。

当前框来自当前帧测量，失检时不绘历史框；证据不足、结构截断或无法消除的几何竞争会拒绝，方向不唯一可保持 unknown。一般透视、任意尺度/成像条件和真实角点精度仍有适用范围限制；本轮小修没有解决闪烁或重定预算，详见[fix2-sweep 原理调查](docs/fix2_sweep_report.md)与[后续小修验收](docs/fix2_followup_acceptance.md)。

## 文档导航

- [完整文档索引](docs/INDEX.md)
- [后续小修施工记录](docs/fix2_followup_log.md)与[验收结果](docs/fix2_followup_acceptance.md)
- [修改前 README 历史全文](docs/history/README_before_fix2_followup.md)：保留 Block3/4/5、Final-fixes、Path A 的全部历史说明和命令。
