# 位姿状态收尾验证

基线a392bd4；[报告](../../POSE_STATUS_FINALIZATION_REPORT.md)。新观测schema6、轨迹schema2。RAW和条件轨迹与[上一轮归档](../single-candidate/README.md)逐字节相同，本目录复用它们，不复制相同数据。日志只去掉行尾空白。

- `observations.yaml.gz`：实际应用的新schema6观测，默认/条件profile两臂字节相同。
- `results.json`：所有名义rvec/平移/RMSE、分歧矩阵与原公式精确比较，以及RAW/G4字节比较。
- `checks.json`：新输出/基线输出的SHA、profile、构建开关、保护分支指针、视频SHA与独立回放检查。
- `ippe-counts.tsv.gz`与log：每次检测的frame/raw_solutions/legal_solutions，实际C++ OpenCV 4.6.0计数，没有生产trace。
- `summary-*.yaml`、`replay-*.log`：两臂各1676帧的实际应用回放。
- `build*.log`/`test*.log`：四种构建、三组CTest日志。
- `ffprobe.json`：独立读完MJPG AVI的1676帧。
- `profile.yaml`：只含条件诊断所用R/q/vv；生产profile仍空。
- `SHA256SUMS`：本目录文件校验值，自身除外。

## 复现

从此交付版本的仓库根目录构建（工具仅在测试构建中）：

```bash
cmake -S src/tushenghao -B /tmp/pose-repeat-build -DCMAKE_BUILD_TYPE=Release \
  -DMARK_BUILD_G2=ON -DMARK_BUILD_G4=ON -DBUILD_TESTING=ON
cmake --build /tmp/pose-repeat-build -j4
ctest --test-dir /tmp/pose-repeat-build --output-on-failure
```

复制当前`src/tushenghao/config/`的五个文件到/tmp/pose-repeat-config。detector.yaml的output添加`run_directory: "/tmp/pose-repeat-output"`，该输出目录必须尚不存在。条件臂把本目录profile.yaml复制到同一配置目录，并把tracking.yaml的`profile: ""`改为`profile: "profile.yaml"`。默认臂保持空profile，使用另一个新输出目录。相机/模型文件不变，哈希在checks中记录。

```bash
/tmp/pose-repeat-build/marker_app --video data/raw/marker_video.avi \
  --config /tmp/pose-repeat-config/detector.yaml
/tmp/pose-repeat-build/detection_replay /tmp/pose-repeat-config/detector.yaml \
  data/raw/marker_video.avi /tmp/pose-repeat-raw.tsv
/tmp/pose-repeat-build/ippe_solution_count /tmp/pose-repeat-config/detector.yaml \
  data/raw/marker_video.avi /tmp/pose-repeat-ippe.tsv
/tmp/pose-repeat-build/tracking_replay /tmp/pose-repeat-output/observations.yaml \
  /tmp/pose-repeat-config/tracking.yaml /tmp/pose-repeat-tracks.yaml
cmp /tmp/pose-repeat-tracks.yaml /tmp/pose-repeat-output/tracks.yaml
PYTHONDONTWRITEBYTECODE=1 python3 src/tushenghao/tests/validation/compare_pose_boundary.py \
  --before src/tushenghao/docs/validation/single-candidate/observations.yaml.gz \
  --after /tmp/pose-repeat-output/observations.yaml \
  --before-tracks src/tushenghao/docs/validation/single-candidate/tracks-conditional.yaml.gz \
  --after-tracks /tmp/pose-repeat-output/tracks.yaml \
  --before-raw src/tushenghao/docs/validation/single-candidate/raw.tsv.gz \
  --after-raw /tmp/pose-repeat-raw.tsv --output /tmp/pose-repeat-results.json
ffprobe -v error -count_frames -select_streams v:0 \
  -show_entries stream=codec_name,width,height,nb_read_frames,avg_frame_rate -of json \
  /tmp/pose-repeat-output/overlay.avi
```

比较器只负责读数据和精确比较，不重做滤波。旧schema5由测试侧历史比较器读作基线，生产没有兼容层。当前tracking_replay只读schema6；复现上一轮旧schema5 reader命令需checkout a392bd4源码。

条件profile来自上一轮相同的合成R/q/vv，没有板端或独立速度真值；本次验证的是数值和行为保持。
