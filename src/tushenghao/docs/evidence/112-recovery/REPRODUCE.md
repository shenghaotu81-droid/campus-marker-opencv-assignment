# 112恢复复现

在仓库根目录执行，使用本轮工作树或与总manifest匹配的源码；公开头/YAML必须保持原哈希。旧main基线身份见environment.json和baseline/manifest.json；基线生产源码保存在baseline/source/lib，不把当前OriginalOnly策略当原性能基线。

正式结果已经封存。复现输出使用新目录，避免覆盖本批日志/JSONL/manifest。

```bash
cmake -S src/tushenghao -B /tmp/recovery112-recheck-release -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/recovery112-recheck-release -j4
ctest --test-dir /tmp/recovery112-recheck-release --output-on-failure

# 必须顺序跑，不与编译/其它视频/合成拟合并发；程序内部固定OpenCV单线程。
/tmp/recovery112-recheck-release/recovery112_benchmark src/tushenghao/config/detector_verification.yaml data/raw/marker_video.avi > /tmp/recovery112-recheck-first.jsonl 2> /tmp/recovery112-recheck-first.log
/tmp/recovery112-recheck-release/recovery112_benchmark src/tushenghao/config/detector_verification.yaml data/raw/marker_video.avi > /tmp/recovery112-recheck-second.jsonl 2> /tmp/recovery112-recheck-second.log

/tmp/recovery112-recheck-release/recovery112_synthetic calibration > /tmp/recovery112-recheck-calibration.jsonl 2> /tmp/recovery112-recheck-calibration.log
/tmp/recovery112-recheck-release/recovery112_synthetic test > /tmp/recovery112-recheck-test.jsonl 2> /tmp/recovery112-recheck-test.log

cmake -S src/tushenghao -B /tmp/recovery112-recheck-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/recovery112-recheck-debug -j4
ctest --test-dir /tmp/recovery112-recheck-debug --output-on-failure

# 此命令会重写汇总；先复制整个工作树（含证据），在副本根目录执行。
# 正式封存目录只核查manifest的SHA，不覆盖任何原始结果。
python3 src/tushenghao/tools/validation/recovery112_check.py summarize
```

验收必须检查两个完整1676帧、1088检测、指定112集合恢复、原976status/raw逐值不变、集合外新增0、两次raw相同；最近秩P50/P95/P99如实记录，mean≤25ms、max≤33ms、超33ms为0。没有超时熔断或删帧；如果新的宿主计时超33ms，应记录该次失败。

独立生成器保持固定种子112/7919、25条校准和88条测试（68正/20负），采样先消耗角度/平移随机数再按冻结规则筛选；只有固定2px压力ID2继续拒绝。检查67/68与最大真值误差<2px，20/20负样本拒绝及25次实际周期参数篡改拒绝。

参考枚举器只链接到corner_edge_fit_test。该测试逐项比较39组全部合格弧、通过质量谓词的配对及最终证据，同时检查正反向轮廓、回走和连接浮点边界。模型路由通过实例内显式策略，正式运行不需要任何实验环境变量。
