# 本轮验证材料

主报告：[单候选与156项交付](../../SINGLE_CANDIDATE_SLIMMING_REPORT.md)。基线f1ee955；原改动快照60c4a6f。当前配置/观测/轨迹schema分别为2/5/2。归档文本日志仅去除行尾空白，便于diff审查；测试/运行输出内容未改。

| 材料 | 含义 |
|---|---|
| `results.json` | PnP、角位移、G4覆盖/协方差/NIS、同参考3D残差与P0投影诊断 |
| `checks.json` | 配置/回放一致性、保护分支指针、视频SHA/帧数、构建与负样本结果 |
| `raw.tsv.gz` | 逐帧`frame detection_count [physical P0…P3 xy]`；通过公开Detector导出 |
| `observations.yaml.gz` | 最终实际应用逐帧G2输出（两种profile字节相同） |
| `tracks-conditional.yaml.gz` | 条件G4真实应用输出；与测试reader调用公开G4结果字节一致 |
| `config-default/`、`config-conditional/` | 回放所用配置；条件profile只保留原合成R/q/vv。输出绝对路径是当次目录 |
| `summary-*.yaml`、`replay-*.log` | 两次1676帧实际应用摘要与日志 |
| `*-{build,config,test}.log`、`build.log`等 | 纯检测/G2/完整链/纯库配置、编译和CTest日志 |
| `ffprobe-conditional.json` | 独立全帧视频解码：MJPG、1440×1080、1676帧 |
| `negative-raw.tsv.gz`、`negative.log` | 6966帧现有无MARK标定视频，0次检测 |
| `frozen-old-guidance-*` | 4360次旧指导输入的直接新拟合，6次无支持，不等于当前quad管线失败 |
| `six-scales*` | 六尺度顺序贡献；工作图结构匹配统计，不是角点候选路由 |
| `ablation-*.log` | 单尺度、无修复、7→6修复、四尺度小抖动测试消融 |
| `experiments/` | 未采纳/开发期只读试验数据与旧API源码；不进入当前构建 |
| `SHA256SUMS` | 本目录除自身外各文件SHA256 |

`experiments/support-trial-*.cpp`使用60c4a6f接口和当时临时路径，说明开发试验来源；不能当当前生产实现编译。最终验证必须使用下述当前公开接口工具。`tracking-uniform-weight-results.json`是淘汰的等权R方案，不是本轮交付结果。

## 从仓库根目录复跑

```bash
cmake -S src/tushenghao -B /tmp/mark-repeat-build -DCMAKE_BUILD_TYPE=Release \
  -DMARK_BUILD_G2=ON -DMARK_BUILD_G4=ON -DBUILD_TESTING=ON
cmake --build /tmp/mark-repeat-build -j4
ctest --test-dir /tmp/mark-repeat-build --output-on-failure
```

复制`config-conditional`到一个临时配置目录，把detector.yaml的output.run_directory改成**尚不存在**的输出目录（例如/tmp/mark-repeat-output）；模型/相机/profile相对依赖保持同目录。默认不带profile的臂同理复制config-default。然后：

```bash
/tmp/mark-repeat-build/marker_app --video data/raw/marker_video.avi \
  --config /tmp/mark-repeat-config/detector.yaml
/tmp/mark-repeat-build/detection_replay /tmp/mark-repeat-config/detector.yaml \
  data/raw/marker_video.avi /tmp/mark-repeat-raw.tsv
/tmp/mark-repeat-build/tracking_replay /tmp/mark-repeat-output/observations.yaml \
  /tmp/mark-repeat-config/tracking.yaml /tmp/mark-repeat-tracks.yaml
cmp /tmp/mark-repeat-output/tracks.yaml /tmp/mark-repeat-tracks.yaml
python3 src/tushenghao/tests/validation/replay_compare.py \
  --before-poses src/tushenghao/docs/validation/g01/observations-final.yaml.gz \
  --after-poses /tmp/mark-repeat-output/observations.yaml \
  --before-tracks src/tushenghao/docs/validation/g01/tracks-conditional.yaml.gz \
  --after-tracks /tmp/mark-repeat-output/tracks.yaml \
  --before-raw src/tushenghao/docs/validation/fixed-mapping/raw-after.tsv.gz \
  --after-raw /tmp/mark-repeat-raw.tsv \
  --camera /tmp/mark-repeat-config/calibration.yaml --output /tmp/mark-repeat-results.json
ffprobe -v error -count_frames -select_streams v:0 \
  -show_entries stream=codec_name,width,height,nb_read_frames,avg_frame_rate -of json \
  /tmp/mark-repeat-output/overlay.avi
```

Python比较器只依赖现有numpy与cv2，不导入生产reader。统计P95按ceil(.95*n)−1；基线候选先排除非数值/非正深度，再取最小RMSE，未按旧eligible预算截断最佳解。G4同参考对比明确不是外部真值。

冻结反例整体检查（预期4360 calls、6无支持；不是以0失败作通过条件）：

```bash
gzip -dc src/tushenghao/docs/validation/match-only/frozen-inputs-4360.txt.gz > /tmp/mark-old-guidance.txt
/tmp/mark-repeat-build/frozen_fit_replay /tmp/mark-old-guidance.txt /tmp/mark-old-guidance-result.tsv
```

最关键的784/P1已是正常CTest的一部分，不依赖临时文件。额外负视频可用同一`detection_replay`替换输入为`data/raw/calibration_video.avi`。
