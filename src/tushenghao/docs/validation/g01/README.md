# G-01／G-02复现材料

完整结论见[报告](../../G01_G02_VALIDATION_REPORT.md)。本目录仅用于离线分析；生产不读取本目录，不改变G4数学、阈值或profile。原视频保留在`data/raw/marker_video.avi`，不重复入Git。

| 文件 | 用途 |
|---|---|
| observations-baseline.yaml.gz／observations-final.yaml.gz | a582c48与新G2的完整1676帧协议；解压后的SHA见manifest |
| baseline-candidates.json | 基线统计、1081对相邻帧旋转配对；完整候选在YAML中 |
| threshold-sweep-initial.txt | 首次只读探索日志；含未采用的“先取绝对预算单支”方案，不代表最终小差距保护语义 |
| results.json／analyze.py | 现行完整结果、候选逐值对照、门前NIS与相关性分析；P95采用nearest-rank |
| tracks-conditional.yaml.gz／summary-*.yaml | 实际marker_app原G4条件回放、生产空profile摘要 |
| nis-pre-gate.csv.gz／nis_probe.cpp | 所有1676帧门前诊断；探针调用同一生产CvFilter，由真实TargetTracker决定更新／重建 |
| nis-probe-check.txt | live位置／完整P／公开速度／接受NIS最大差均0；探针tracks与实际输出hash相同 |
| tracking-conditional.yaml／reference-profile.yaml | 临时条件配置和逐字节原profile副本，camera_id仍synthetic-camera-g4；不属于生产配置 |
| ffprobe-*.json／*.log／manifest.json | 两个AVI完整解码、各构建／测试和输入输出SHA256 |

以下从仓库根目录执行，输出需新目录；Python分析需要cv2与numpy，生产不依赖Python。先按README构建全链。

```bash
mkdir -p /tmp/g01-reproduce
gzip -dc src/tushenghao/docs/validation/g01/observations-baseline.yaml.gz > /tmp/g01-reproduce/before.yaml
gzip -dc src/tushenghao/docs/validation/g01/observations-final.yaml.gz > /tmp/g01-reproduce/after.yaml
gzip -dc src/tushenghao/docs/validation/g01/nis-pre-gate.csv.gz > /tmp/g01-reproduce/nis.csv
python3 src/tushenghao/docs/validation/g01/analyze.py --before /tmp/g01-reproduce/before.yaml --after /tmp/g01-reproduce/after.yaml --nis /tmp/g01-reproduce/nis.csv --output /tmp/g01-reproduce/results.json
```

重跑当前录像：复制`src/tushenghao/config/`到新临时目录，在detector.yaml中只指定新`output.run_directory`。运行`build/marker_app --video data/raw/marker_video.avi --config <临时目录>/detector.yaml`。默认tracking.yaml保持空profile。条件重验另复制本目录reference-profile.yaml，把临时tracking.yaml的profile设为`reference-profile.yaml`，再用另一个新输出目录跑同一输入；不改profile身份或任何数值。两种配置的observations应逐字节相同。

门前探针（仅本片的camera-relative、未知曝光时间、单Unique目标条件；不适用多目标/世界变换数据）：

```bash
c++ -O3 -std=c++17 src/tushenghao/docs/validation/g01/nis_probe.cpp -Isrc/tushenghao/include -Isrc/tushenghao/src/tracking -I/usr/include/opencv4 build/src/tracking/libvision_tracking.a build/src/pose/libvision_pose.a -lapriltag build/src/config/libvision_config.a build/src/math/libvision_math.a -lopencv_videoio -lopencv_imgcodecs -lopencv_calib3d -lopencv_features2d -lopencv_imgproc -lopencv_core -o /tmp/g01-reproduce/nis_probe
/tmp/g01-reproduce/nis_probe /tmp/g01-reproduce/after.yaml src/tushenghao/docs/validation/g01/tracking-conditional.yaml /tmp/g01-reproduce/nis
ffprobe -v error -count_frames -select_streams v:0 -show_entries stream=codec_name,codec_tag_string,width,height,nb_read_frames -of json /tmp/g01-conditional-replay/overlay.avi
```

基线源代码需另行只读检出a582c48、独立构建，在该配置副本中只改输出目录；保留当时overlay.mp4并确认基线hash。这里的archive YAML可直接复算，不需切换当前分支或重写任何历史证据。
