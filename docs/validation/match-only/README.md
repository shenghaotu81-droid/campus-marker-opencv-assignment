# match-only 验证数据

完整结论与局限见[本轮报告](../../MATCH_ONLY_REFACTOR_REPORT.md)。这些是测试／回放材料，生产不读取它们。

- baseline-4360.tsv.gz：b600635的11路线结果，4359次路线1＋1次路线4。
- ablation-two-routes.tsv.gz：仅并路线，除路线号4→2外所有数值完全相同。
- frozen-inputs-4360.txt.gz：从b600635真实视频处理得到的4360个原图轮廓及边提示；无可执行对应／仿射逻辑。
- ablation-weighted-old-edges.tsv.gz：**当前**拟合器直接读上述输入的结果，4359次主路＋1次周期后验，0失败。
- final-4352.tsv.gz：最终match-only主链的1088个独立框×4角；全L2。
- synthetic-{before,after}.tsv.gz、synthetic-truth.json：72视角的原始结果和独立渲染真值；完整帧43→62，10个实际出框视角不发布。
- observations-{before,after}.yaml.gz：同G2/G4数值配置的完整1676帧位姿协议，不强选Ambiguous候选。
- final-tracks.yaml.gz：同配置前后完全相同；生产profile为空，1676帧NOT_READY，没有真实track。
- summary.yaml、metrics.json、checks.json、manifest.json：实际运行摘要、机器统计、交叉检查及压缩前后SHA256。

TSV无表头，列依次为：frame_id、匹配索引（旧表为父候选索引）、P0…P3编号、路线号、x、y、两条raw平均垂距、交点到实测连接弧的距离、两条extension；坐标和距离均为原图px。失败记录第四列FAIL，后续无数值。旧frame313和382各有第二个父候选，新版不重复产生。

冻结输入为重复记录：一行 `frame_id match_index physical_corner contour_point_count`，随后4行原图边提示端点（每行x y，两个边各两个端点），然后N行原图轮廓整数像素（x y）。帧号只用于输出索引，不传入拟合器。

```bash
gzip -dc docs/validation/match-only/frozen-inputs-4360.txt.gz > /tmp/frozen-inputs.txt
build/corner_route_test --frozen /tmp/frozen-inputs.txt /tmp/current-frozen.tsv
```

`replay_metrics.py compare`可直接读这里的gzip，完整命令见报告。P95采用nearest-rank，非线性插值。真实录像无独立角真值；新旧角位移不等于定位误差。synthetic真值不提供给检测／拟合。当前脚本生成器使用Python OpenCV5.0.0，生产与C++测试为OpenCV4.6.0；生成72视角AVI的SHA256为`9dbe3cc59c252b9546fd53ba80fa554b43a9294f6717d602fc241dddc3c9b237`。
