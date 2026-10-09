# 独立取证复现

所有可执行代码在 `/tmp` 编译，生产代码不变。为防覆盖归档，先建立只含证据副本的独立工作目录（下例目录必须不存在）。`src` 和 `data` 只读使用当前工作区内容；没有切换 git 分支。执行前核对生产 HEAD、三配置 SHA256 和原始视频 SHA256 与 manifest 一致。将 WORKSPACE 换成当前仓库的绝对路径。

```bash
WORKSPACE=/home/tushenghao/projects/campus-marker-opencv-assignment
mkdir -p /tmp/marker112-replay/docs/evidence
ln -s "$WORKSPACE/src" /tmp/marker112-replay/src
ln -s "$WORKSPACE/data" /tmp/marker112-replay/data
cp -a "$WORKSPACE/docs/evidence/112-forensics" /tmp/marker112-replay/docs/evidence/
cd /tmp/marker112-replay
mkdir -p /tmp/marker112-replay-code
python3 - <<'PY'
from pathlib import Path
for p in Path('docs/evidence/112-forensics/deep/method').glob('*.txt'):
    Path('/tmp/marker112-replay-code',p.name[:-4]).write_text(p.read_text())
PY
cmake -S src/tushenghao -B /tmp/marker112-replay-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/marker112-replay-build --target marker_app -j4
c++ -O2 -std=c++17 /tmp/marker112-replay-code/marker112_extract.cpp -I src/tushenghao/include -I src/tushenghao/lib /tmp/marker112-replay-build/libmark_detector.a $(pkg-config --cflags --libs opencv4) -o /tmp/marker112-replay-code/extract
/tmp/marker112-replay-code/extract src/tushenghao/config/detector_verification.yaml data/raw/marker_video.avi docs/evidence/112-forensics/deep/selected_ids.txt docs/evidence/112-forensics/deep/corner_inputs.txt
c++ -O2 -std=c++17 /tmp/marker112-replay-code/marker112_probe_main.cpp /tmp/marker112-replay-code/marker112_probe_fit.cpp -I src/tushenghao/include -I src/tushenghao/lib /tmp/marker112-replay-build/libmark_detector.a $(pkg-config --cflags --libs opencv4) -o /tmp/marker112-replay-code/probe
/tmp/marker112-replay-code/probe src/tushenghao/config/detector_verification.yaml docs/evidence/112-forensics/deep/corner_inputs.txt base > docs/evidence/112-forensics/deep/probe-base.jsonl
for MODE in residual position direction connection extension tls unique endpoints; do
  /tmp/marker112-replay-code/probe src/tushenghao/config/detector_verification.yaml docs/evidence/112-forensics/deep/failed_corner_inputs.txt "$MODE" > "docs/evidence/112-forensics/deep/probe-$MODE.jsonl"
done
/tmp/marker112-replay-code/probe src/tushenghao/config/detector_verification.yaml docs/evidence/112-forensics/deep/failed_corner_inputs.txt residual > docs/evidence/112-forensics/deep/probe-residual-evidence.jsonl
c++ -O2 -std=c++17 /tmp/marker112-replay-code/marker112_pipeline.cpp /tmp/marker112-replay-code/marker112_probe_fit.cpp -I src/tushenghao/include -I src/tushenghao/lib /tmp/marker112-replay-build/libmark_detector.a $(pkg-config --cflags --libs opencv4) -o /tmp/marker112-replay-code/pipeline
/tmp/marker112-replay-code/pipeline src/tushenghao/config/detector_verification.yaml docs/evidence/112-forensics/deep/selected_ids.txt positionfit > docs/evidence/112-forensics/deep/pipeline-positionfit.jsonl
/tmp/marker112-replay-code/pipeline src/tushenghao/config/detector_verification.yaml docs/evidence/112-forensics/deep/selected_ids.txt cascade > docs/evidence/112-forensics/deep/pipeline-cascade.jsonl
/tmp/marker112-replay-code/pipeline src/tushenghao/config/detector_verification.yaml docs/evidence/112-forensics/deep/all_frame_ids.txt cascade > docs/evidence/112-forensics/deep/pipeline-cascade-full.jsonl
c++ -O2 -std=c++17 /tmp/marker112-replay-code/marker112_photometric.cpp -I src/tushenghao/lib $(pkg-config --cflags --libs opencv4) -o /tmp/marker112-replay-code/photo
/tmp/marker112-replay-code/photo docs/evidence/112-forensics/deep/probe-residual-evidence.jsonl > docs/evidence/112-forensics/deep/photometric-refit.jsonl
python3 /tmp/marker112-replay-code/marker112_envelope.py
python3 /tmp/marker112-replay-code/marker112_scanline.py
python3 /tmp/marker112-replay-code/marker112_periodic.py
```

`marker112_periodic_cv.py` 使用 `/tmp/marker112_periodic.py` 的共享函数。复现时将已归档版本复制到该临时路径，然后运行：

```bash
cp /tmp/marker112-replay-code/marker112_periodic.py /tmp/marker112_periodic.py
python3 /tmp/marker112-replay-code/marker112_periodic_cv.py
python3 /tmp/marker112-replay-code/marker112_deep_summary.py
```

绘图程序独立编译，只需 OpenCV。`marker112_render.cpp` 位置参数是失败角输入、residual-evidence 和输出 images 目录；`marker112_recovered_render.cpp` 使用证据目录中的最终整链 JSONL 和 recovered_ids。

取证拟合器在此独立程序中提供与原函数同名的实现，链接静态库时由取证目标定义该符号；生产库其余实现保持不变。base 等价比较为必要验收，不能略过。cascade 顺序为：原始拟合成功立即原样返回；失败后试后验有限段位置判据；再失败试所有真实端点。fallback 不放宽残差、方向、连接、角误差、延伸、跨度或点数预算。

时间源必须显式设置 `TimestampSource::Unknown`（源码已设置）；任何 INVALID_INPUT/NOT_READY 记录不计为视觉失败。归档的所有最终整链行均已通过该检查。位置信息的帧 784 复算用独立双精度 TLS；记录注明与生产 cv::fitLine 不是同一浮点求值。

数值分布是按调用合并的精确最小/最大值和定宽直方图，区间为 `[index*step,(index+1)*step)`；残差 step=0.05，其余 step=1。原始短路门控“未执行”不伪造成通过；位置全拒弧记录对两边的完整最大距离之较小者；方向值只来自已经通过前置门控的弧；连接完整长度额外以轮廓弧长前缀表复算，生产计数仍按原短路逻辑。大量候选本就是错误组合，因此不能把近阈值弧比例直接当真目标恢复比例。
