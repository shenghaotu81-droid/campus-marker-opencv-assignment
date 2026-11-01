# 板块1 验收记录

> 历史状态：下文保留板块 1 当时的命名、配置与验收结论。现行模式为 `SegmentedMark`，显示键归属 `display`；当前接口见 [数据流与接口](dataflow-interfaces.md)。

## 1. 完成内容

本板块完成 Detector 配置骨架与基础工程链路：

- 6 个头文件 + `config.cpp` 三个核心函数 + `detector.cpp` 骨架
- `Detector` 构造时进行 `validateConfig` 二次校验
- 完成 4 个测试：
  - `config_contract_test`（占位测试）
  - `config_error_test`（5 case）
  - `config_roundtrip_test`
  - `detector_contract_test`
- 完成 `detector.yaml` 14 个配置字段
- 完成 `main --check-config` 全链路验证

当前链路：

```
detector.yaml
    ↓
loadConfig()
    ↓
validateConfig()
    ↓
Detector(config)
    ↓
成功退出
```

---

## 2. 验证方法

构建：

```bash
cmake -S src/tushenghao -B build/tushenghao
cmake --build build/tushenghao
```

运行测试：

```bash
ctest --test-dir build/tushenghao --output-on-failure
```

检查配置：

```bash
./build/tushenghao/marker_app --check-config
```

预期结果：

```
100% tests passed
```

以及：

```
Config check passed
```

---

## 3. 踩坑记录

### 坑1：OpenCV YAML 行尾注释问题

现象：

`detector.yaml` 中：

```yaml
mode: Skeleton  # 注释
```

使用 OpenCV `cv::FileStorage` 读取后，`#` 后面的内容没有被当作注释处理，而是进入字符串。

导致：

- `mode.size()` 读取结果异常
- 预期 `"Skeleton"` 长度为 8，但实际读出 size=59
- `mode == "Skeleton"` 判断失败

排查过程：

通过打印：

- `mode.size()`
- 每个字符对应的 ASCII 数值

确认字符串中混入了额外内容。

解决：

删除 YAML 行尾 `#` 注释。

经验：

OpenCV `FileStorage` YAML 解析行为与标准 YAML 不完全一致，配置文件中避免使用行尾注释。

---

### 坑2：Commit hygiene

问题：

提交前使用：

```bash
git diff --check
```

发现多个文件存在：

```
trailing whitespace
```

即行尾多余空格。

处理：

使用：

```bash
sed -i 's/[[:space:]]*$//' $(git diff --name-only)
```

清理修改文件中的行尾空格。

再次执行：

```bash
git diff --check
```

确认无输出。

经验：

提交前检查 diff 是工程习惯，避免格式问题进入仓库，也避免后续 CI 检查失败。

---

### 坑3：冻结表是 Source of Truth

问题：

冻结字段规定：

```
detector.mode = "Skeleton"
```

但是 `config.cpp` 初始解析逻辑中写成：

```cpp
"skeleton"
```

导致：

- YAML 内容正确
- 代码比较字符串错误
- `unsupported mode` 报错

排查后发现代码与冻结表不一致。

解决：

修改代码：

```cpp
if (mode == "Skeleton")
```

原则：

冻结表（Source of Truth）优先于代码实现。

如果代码与冻结定义冲突，应修改代码，而不是修改配置规范。

---

## 4. 已知限制

- `--smoke-test` 尚未实现，目前使用 `--check-config` 验证配置加载与 Detector 构造链路。
- 未验证 OpenCV FileStorage 对未知字段、重复字段的处理行为。