# 沙盒结果复现

在仓库根目录执行。输出目录示例使用 `/tmp/marker112-recheck-output`，避免覆盖本轮证据。输入 hash、HEAD、源 hash 和工具链先与 `manifest.json` 核对。不切换分支，不执行 checkout/commit/push。以下命令可在当前沙盒工作树复现最终版本；main 的实施不依赖这些临时机制，按 `EXECUTION_PLAN.md` 第 3 节实现。

```bash
mkdir -p /tmp/marker112-recheck-output
cmake -S src/tushenghao -B /tmp/marker112-recheck-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/marker112-recheck-build -j4
c++ -O3 -DNDEBUG -std=c++17 docs/evidence/112-sandbox/method/benchmark.cpp -I src/tushenghao/include -I src/tushenghao/lib /tmp/marker112-recheck-build/libmark_detector.a $(pkg-config --cflags --libs opencv4) -o /tmp/marker112-recheck-benchmark
MARK_SANDBOX_ROUTE=ab MARK_SANDBOX_MODEL=c1median /tmp/marker112-recheck-benchmark src/tushenghao/config/detector_verification.yaml 1 > /tmp/marker112-recheck-output/final.jsonl
MARK_SANDBOX_ROUTE=ab MARK_SANDBOX_MODEL=c1median /tmp/marker112-recheck-benchmark src/tushenghao/config/detector_verification.yaml 1 > /tmp/marker112-recheck-output/final-repeat.jsonl
MARK_SANDBOX_ROUTE=ab MARK_SANDBOX_MODEL=c1median MARK_SANDBOX_PROFILE=1 /tmp/marker112-recheck-benchmark src/tushenghao/config/detector_verification.yaml 1 > /tmp/marker112-recheck-output/profile.jsonl
MARK_SANDBOX_ROUTE=ab /tmp/marker112-recheck-benchmark src/tushenghao/config/detector_verification.yaml 1 > /tmp/marker112-recheck-output/ab.jsonl
MARK_SANDBOX_ROUTE=ab MARK_SANDBOX_MODEL=c1 /tmp/marker112-recheck-benchmark src/tushenghao/config/detector_verification.yaml 1 > /tmp/marker112-recheck-output/c1-ols.jsonl
MARK_SANDBOX_ROUTE=ab MARK_SANDBOX_MODEL=c2 /tmp/marker112-recheck-benchmark src/tushenghao/config/detector_verification.yaml 1 > /tmp/marker112-recheck-output/c2.jsonl
ctest --test-dir /tmp/marker112-recheck-build --output-on-failure
MARK_SANDBOX_ROUTE=ab MARK_SANDBOX_MODEL=c1median ctest --test-dir /tmp/marker112-recheck-build --output-on-failure
```

每个公共 process 运行必须有连续 frame 0..1675、1676 行，结果必须是 976/996/1082/1074/1088 等对应版本的检测数；最终新增集合必须严格等于 deep/sample_definition.json 的 failed_ids，原 976 帧 raw 四角逐值不变。Summary 诊断用于阶段计时，VideoCapture/JSON 序列化在 process 计时之外。CPU 性能运行按顺序执行，不与编译/其他测试并行。

原库基线不要用沙盒 `MARK_SANDBOX_ROUTE=baseline` 当性能基线：该模式已经有缓存/等价搜索优化，只能用于优化后原结果契约检查。**完整原始性能基线必须从未修改的 HEAD 导出另一个源码目录构建。**以下 git archive 是只读导出，不会切换工作树分支。

```bash
mkdir -p /tmp/marker112-original-source
 git archive 81cef2b4d0c3c29ef9d5d9cc550dc148b87232b0 src/tushenghao/CMakeLists.txt src/tushenghao/lib src/tushenghao/include src/tushenghao/config src/tushenghao/app src/tushenghao/tests src/tushenghao/test_support src/tushenghao/tools | tar -x -C /tmp/marker112-original-source
cmake -S /tmp/marker112-original-source/src/tushenghao -B /tmp/marker112-original-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/marker112-original-build --target mark_detector -j4
c++ -O3 -DNDEBUG -std=c++17 docs/evidence/112-sandbox/method/benchmark.cpp -I /tmp/marker112-original-source/src/tushenghao/include -I /tmp/marker112-original-source/src/tushenghao/lib -I src/tushenghao/lib /tmp/marker112-original-build/libmark_detector.a $(pkg-config --cflags --libs opencv4) -o /tmp/marker112-original-benchmark
/tmp/marker112-original-benchmark src/tushenghao/config/detector_verification.yaml 1 > /tmp/marker112-recheck-output/baseline.jsonl
```

最后一个 include 路径仅供独立 benchmark 使用 profiling header；原库本身来自 archive 的原始源，不能把当前修改过的库作为原始库。重新生成后的基线 JSON 会带零函数计数字段；实际原始基线二进制的历史日志不带该字段，不影响检测/阶段/外部 process 耗时对账。

独立精度、缺失支持、参数篡改验证：

```bash
c++ -O3 -DNDEBUG -std=c++17 src/tushenghao/tools/validation/sandbox_model_verify.cpp -I src/tushenghao/include -I src/tushenghao/lib -I src/tushenghao/test_support /tmp/marker112-recheck-build/libmark_detector.a $(pkg-config --cflags --libs opencv4) -o /tmp/marker112-recheck-model-verify
MARK_SANDBOX_ROUTE=ab MARK_SANDBOX_MODEL=c1median /tmp/marker112-recheck-model-verify calibration > /tmp/marker112-recheck-output/calibration.jsonl
MARK_SANDBOX_ROUTE=ab MARK_SANDBOX_MODEL=c1median /tmp/marker112-recheck-model-verify test > /tmp/marker112-recheck-output/test-c1.jsonl
MARK_SANDBOX_ROUTE=ab MARK_SANDBOX_MODEL=c2 /tmp/marker112-recheck-model-verify test > /tmp/marker112-recheck-output/test-c2.jsonl
MARK_SANDBOX_ROUTE=baseline /tmp/marker112-recheck-model-verify test > /tmp/marker112-recheck-output/test-baseline.jsonl
c++ -O3 -std=c++17 docs/evidence/112-sandbox/method/chain_budget.cpp -I src/tushenghao/lib $(pkg-config --cflags --libs opencv4) -o /tmp/marker112-recheck-chain-budget
/tmp/marker112-recheck-chain-budget > /tmp/marker112-recheck-output/chain-budget.jsonl
```

固定校准 25 行、测试 88 行（68 正、20 负）。0/1 px 正样本全部保留，2 px 高成本压力每个 split 固定 id2；测试集采样在首次执行测试前冻结。初始更大校准被停止的完整记录保存在 independent-calibration-exploratory.jsonl，不能当作完整 36/120 实验。

`method/replay.cpp` 读取前轮已归档的固定完整轮廓输入，重放 153/382/784；`method/render.cpp` 对 final-repeat2.jsonl 及原视频生成联系表。它们输出到固定 evidence 路径，复现时先在独立目录复制输入和证据，以免覆盖原档。

`method/summarize.py` 在仓库根运行，会对已归档版本行数、恢复集合、旧 raw 全等、最终两次输出一致作断言并写 summary.json；本轮所有断言实际执行。manifest 自身不包含在自身 hash 中；校验方法是逐项重新 SHA256 与 manifest 比较。
