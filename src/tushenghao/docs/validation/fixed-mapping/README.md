# 固定映射完整对照证据

结论与安装核验见[报告](../../FIXED_MAPPING_REPORT.md)。`checks.json`记录1676帧逐值比较与完整文件SHA；`raw-{before,after}.tsv.gz`逐字节相同。TSV每行：帧号、Status枚举值、检测数量，然后每个Detection的四角float x/y、质量旗、P→屏幕映射四个整数。探针只读调用真实decode pipeline，无检测或位姿反馈。

基线10eae9b的完整位姿与条件G4协议已归档于[上批validation/g01](../g01/README.md)，本轮输出与其逐字节相同，避免重复存储。`summary-*.yaml`及日志是最终解耦布局的实际复跑结果；summary只有总耗时变化。默认生产profile为空；条件profile逐字节复用g01/reference-profile.yaml，未改身份或数值。当前视频本地路径为`/tmp/fixed-default-final-replay/overlay.avi`及`/tmp/fixed-conditional-final-replay/overlay.avi`，两者各与对应基线AVI逐字节一致；基线1676帧完整解码证据位于g01，exporter本次也完整读回1676帧。

复现：按个人README构建全链，复制`src/tushenghao/config`到新临时目录，在detector.yaml的output中填一个不存在的run_directory；输入同一`data/raw/marker_video.avi`。条件回放仅在临时tracking.yaml里设置profile并复制上批原profile，不写生产配置。运行：

```bash
build/marker_app --video data/raw/marker_video.avi --config /tmp/fixed-default-config/detector.yaml
```

上述已用目录需要改名以避免覆盖。重跑后用`cmp`对照完整observations、tracks及AVI；summary先去除elapsed_s行比较。原视频SHA256为`aa1219a7a7b702ea1265be8752853a267c982a651afa0f7846f516f9517c9ac7`。

只读RAW探针（从仓库根执行）：

```bash
c++ -O3 -std=c++17 src/tushenghao/docs/validation/fixed-mapping/raw_probe.cpp -Isrc/tushenghao/include -Isrc/tushenghao/src/config -Isrc/tushenghao/src/detection -I/usr/include/opencv4 build/src/detection/libvision_detection.a build/src/config/libvision_config.a build/src/math/libvision_math.a -lopencv_videoio -lopencv_imgcodecs -lopencv_imgproc -lopencv_core -o /tmp/raw_probe
/tmp/raw_probe src/tushenghao/config/detector.yaml data/raw/marker_video.avi /tmp/raw.tsv
gzip -dc src/tushenghao/docs/validation/fixed-mapping/raw-before.tsv.gz > /tmp/raw-baseline.tsv
cmp /tmp/raw-baseline.tsv /tmp/raw.tsv
```

基线RAW在本轮改代码之前，用10eae9b的库运行同一探针采集。若从头复现基线，可将10eae9b归档解包到独立临时目录构建，探针链接那套库；无需切换或修改三个受保护分支。测试日志覆盖全链17项、仅检测13项、G2不含G4的15项和仅库构建；不将桌面结果当作MaixCam安装验收。
