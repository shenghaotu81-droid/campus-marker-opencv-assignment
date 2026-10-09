# 最慢帧复现与验收

在仓库根目录执行；不切换分支、不commit/push。输出到另一个目录，避免覆盖归档。核对manifest中输入视频、YAML、生成器和最终源码SHA256。计时协议与上轮一致，OpenCV固定1线程，公共process包含Summary，VideoCapture/JSON序列化不在计时内。

```bash
mkdir -p /tmp/marker112-worst-recheck
cmake -S src/tushenghao -B /tmp/marker112-worst-recheck-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/marker112-worst-recheck-build -j4
c++ -O3 -DNDEBUG -std=c++17 docs/evidence/112-sandbox/method/benchmark.cpp -I src/tushenghao/include -I src/tushenghao/lib /tmp/marker112-worst-recheck-build/libmark_detector.a $(pkg-config --cflags --libs opencv4) -o /tmp/marker112-worst-recheck/benchmark
# profiling必须完全unset；设成字符串0仍会启用计时。
unset MARK_SANDBOX_PROFILE
MARK_SANDBOX_ROUTE=ab MARK_SANDBOX_MODEL=c1median MARK_WORST_ROUTE=phase_first /tmp/marker112-worst-recheck/benchmark src/tushenghao/config/detector_verification.yaml 1 > /tmp/marker112-worst-recheck/final1.jsonl
MARK_SANDBOX_ROUTE=ab MARK_SANDBOX_MODEL=c1median MARK_WORST_ROUTE=phase_first /tmp/marker112-worst-recheck/benchmark src/tushenghao/config/detector_verification.yaml 1 > /tmp/marker112-worst-recheck/final2.jsonl
MARK_SANDBOX_PROFILE=1 MARK_SANDBOX_ROUTE=ab MARK_SANDBOX_MODEL=c1median MARK_WORST_ROUTE=phase_first /tmp/marker112-worst-recheck/benchmark src/tushenghao/config/detector_verification.yaml 1 > /tmp/marker112-worst-recheck/profile.jsonl
# 原搜索优先级：完整输出与上轮全等，但该路线本轮未满足最大33ms。
MARK_SANDBOX_ROUTE=ab MARK_SANDBOX_MODEL=c1median MARK_WORST_ROUTE=legacy /tmp/marker112-worst-recheck/benchmark src/tushenghao/config/detector_verification.yaml 1 > /tmp/marker112-worst-recheck/exact.jsonl
# 原算法结果契约：976检测，不能当成未优化原库的性能基线。
MARK_SANDBOX_ROUTE=baseline /tmp/marker112-worst-recheck/benchmark src/tushenghao/config/detector_verification.yaml 1 > /tmp/marker112-worst-recheck/baseline.jsonl
```

按顺序运行，性能测量时不与编译、别的拟合/视频实验或渲染并发；没有绑核、加工作线程、快数学或改阈值。不同宿主负载会影响实际墙钟分布，不靠删除异常帧通过验收。

全部性能运行必须连续frame0..1675共1676行；最终检测1088，新增集合严格等于deep/sample_definition.json的112个failed_ids；原976的status/raw角点全等；最终两次raw输出一致；mean≤25ms、max≤33ms、超33ms计数0。若复现的最慢帧超33，就如实判该次未达标，不能四舍五入或忽略该帧。本轮两次正式实际结果在REPORT.md和summary.json。

新优先级改变了21个恢复帧的角点；相对旧解最大变化1.628526px，是旧/新解之差，不是视频真值误差。原优先级的exact对照必须与上轮1088逐值全等。manifest指明旧冻结二进制身份；从旧source-final源码快照恢复到独立临时源码目录可重建旧库，不能把当前优化后的baseline路由称作旧冻结库。

固定独立测试（**生成器未修改**）：

```bash
c++ -O3 -DNDEBUG -std=c++17 src/tushenghao/tools/validation/sandbox_model_verify.cpp -I src/tushenghao/include -I src/tushenghao/lib -I src/tushenghao/test_support /tmp/marker112-worst-recheck-build/libmark_detector.a $(pkg-config --cflags --libs opencv4) -o /tmp/marker112-worst-recheck/model_verify
MARK_SANDBOX_ROUTE=ab MARK_SANDBOX_MODEL=c1median MARK_WORST_ROUTE=phase_first /tmp/marker112-worst-recheck/model_verify calibration > /tmp/marker112-worst-recheck/calibration.jsonl
MARK_SANDBOX_ROUTE=ab MARK_SANDBOX_MODEL=c1median MARK_WORST_ROUTE=phase_first /tmp/marker112-worst-recheck/model_verify test > /tmp/marker112-worst-recheck/test.jsonl
MARK_SANDBOX_ROUTE=baseline ctest --test-dir /tmp/marker112-worst-recheck-build --output-on-failure
MARK_SANDBOX_ROUTE=ab MARK_SANDBOX_MODEL=c1median MARK_WORST_ROUTE=phase_first ctest --test-dir /tmp/marker112-worst-recheck-build --output-on-failure
cmake -S src/tushenghao -B /tmp/marker112-worst-recheck-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/marker112-worst-recheck-debug -j4
MARK_SANDBOX_ROUTE=baseline ctest --test-dir /tmp/marker112-worst-recheck-debug --output-on-failure
MARK_SANDBOX_ROUTE=ab MARK_SANDBOX_MODEL=c1median MARK_WORST_ROUTE=phase_first ctest --test-dir /tmp/marker112-worst-recheck-debug --output-on-failure
```

固定25校准、88测试（68正/20负），保持全部ID、seed、旋转/平移、扰动和负样本与上轮逐项一致；67个0/1px正样本全部接受且已知真值误差≤2px，仅固定2px压力id2被拒绝，20个负样本全拒绝，实际25次周期参数篡改全拒绝。Release/Debug四份回归各25/25，不能仅用Release中禁用的assert作验证。

`method/summarize.py`对归档的所有完整版本作独立断言，写summary.json；`method/write_report.py`由汇总生成REPORT并更新EXECUTION_PLAN第5节。对归档目录重跑会改变文档/汇总时间内容，复现时使用副本，避免破坏sealed hashes。

`method/render.cpp`、旧method/replay.cpp用于叠图和冻结输入重放，必须改输出路径或在独立证据副本目录执行；不能覆盖正式档。安装脚本与before阶段源码只是过程记录，main实施依赖设计、纯函数、显式调用内scratch和回归reference，不依赖这些/tmp脚本、环境变量或全局计时状态。
