# Block 2 合成生成器

独立 C++17 工具，按 `inputs/synthetic_gen_spec_Oyster_reviewed.md` 开发。输出测试输入、连续几何真值与审计清单；不运行 Detector，不测算法误差，不生成检测容差或资源配置。工具开发与正式数据交付分别记录在 [验收记录](docs/acceptance.md)。

目前模型和实验没有有效批准记录，`formal` 返回退出码 3。`selftest` 只生成独立矩形与 L 凹形，不生成 MARK 正式数据。原始输入不改写，M 的面积仅在模型单元测试中由顶点计算，不作为算法门限。

## 安装与编译

目标 Linux/WSL Ubuntu，CMake ≥3.16、C++17、OpenCV 4 的 core/imgproc/imgcodecs、nlohmann_json、OpenSSL Crypto。缺依赖时由使用者自行运行：

```bash
sudo apt-get update
sudo apt-get install build-essential cmake libopencv-dev nlohmann-json3-dev libssl-dev
```

所有运行命令从工具根目录执行，不进入 build。输入放在 `inputs/`：`model_coordinates.md`、`error_budget.md`、`2.md`、本 spec 和 `sources.json`。`2.md` 对应原名《板块2_几何原语_完整冻结文档.md》。三份文档原文与 SHA-256 必须对应来源清单，缺失时使用交付原件，不能网络抓取替代或改 hash 掩盖改变。配置在 `configs/`，均为本工具私有 JSON。

```bash
cd ~/projects/robomaster-vision/tools/synthetic_gen
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/synthetic_gen --help
./build/synthetic_gen validate
./build/synthetic_gen plan --run-id plan-example-001
./build/synthetic_gen generate --mode selftest --run-id selftest-example-001
./build/synthetic_gen verify --run-id selftest-example-001
```

每次使用新的 run-id；已有目录会拒绝覆盖。`plan` 可省略 run-id，默认 `plan-<配置hash前12位>`，同配置第二次需明确新名称。每个命令可追加 `--help`，帮助不要求输入和批准。`validate` 检查来源、转录、配置并单列正式资格；输入有效且批准缺失时它成功返回 0，正式资格显示 `BLOCKED`。

从干净构建目录复核（无需删除已有 build）：

```bash
cmake -S . -B build-review -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-review --parallel 2
ctest --test-dir build-review --output-on-failure
./build-review/synthetic_gen validate
./build-review/synthetic_gen plan --run-id plan-review-001
./build-review/synthetic_gen generate --mode selftest --run-id selftest-review-001
./build-review/synthetic_gen verify --run-id selftest-review-001
```

测试使用普通 C++ 可执行程序，Release 中也执行全部检查。`workflow` 在 `/tmp/sg-selftest-*` 隔离副本中测试缺批准、错误 hash、重复生成、subset、删图、损坏 PNG 和 INCOMPLETE；不会改原件或在工具输入中建立批准记录。证据写入所用构建目录的 `test-evidence-workflow.json`，记录临时目录与失败 case_id。

## 正式生成和单例复现

正式运行前，需要用户明确确认模型坐标表（含 D1、D7/D8/D9 关卡）和本版取样/渲染配置。用户或获授权的工程窗口保存真实批准证据为 `inputs/approval.json`，包含：

| 字段 | 内容 |
|---|---|
| `user_confirmation_quote` | 用户明确确认模型及实验配置的原文引用，不能用“开发工具”的指令替代 |
| `model_document_hash` | 原始 `inputs/model_coordinates.md` 的 SHA-256 |
| `model_config_hash` | 私有 model JSON 规范化后的 SHA-256 |
| `experiment_config_hash` | model/grid/cases 三份有效 JSON 联合规范化后的 SHA-256 |

`validate` 会打印后三个 hash，供确认时绑定版本。工具仅验证非空引用与 hash 对应，不提供批准写入命令或自签模板；真实引用的来源由批准记录提供者负责。工具实现授权不代表模型/实验已有批准，资源标签也不是已批准上限。不要创建虚假引用绕过资格检查。输入或配置变更后须重新核对批准绑定。

有效批准记录到位后，从工具根目录执行：

```bash
./build/synthetic_gen generate --mode formal --run-id formal-001
./build/synthetic_gen verify --run-id formal-001
```

完整规划共 **38,784 项**：34,560 全局扰动（含 6,912 基本组合）、4,096 局部扰动、64 剪切、64 特殊项。不是每项都有图片：纯几何/计数 fixture 及非法扰动保存真值与清单，无图像路径。非法扰动不会漏样。`plan` 报所有 family 数量和图像平面未压缩上界（107,230,003,200 字节，约 99.87 GiB；JSON、PNG容器及文件系统额外占空间），实际 PNG 按固定压缩策略保存。

单例 ID 从 `runs/plan-example-001/planned_cases.jsonl` 选择；复制该行 `case_id` 的 64 位十六进制值，替换下例变量内容。单例也需要同一有效批准：

```bash
CASE_ID='替换为计划中的真实case_id'
./build/synthetic_gen generate --mode formal --case-id "$CASE_ID" --run-id formal-one-a
./build/synthetic_gen generate --mode formal --case-id "$CASE_ID" --run-id formal-one-b
./build/synthetic_gen verify --run-id formal-one-a
./build/synthetic_gen verify --run-id formal-one-b
sha256sum runs/formal-one-a/truth.jsonl runs/formal-one-b/truth.jsonl
sha256sum runs/formal-one-a/images/*.png runs/formal-one-b/images/*.png
```

若选的是无图像 fixture，仅比较真值；`images/` 为空。单例 manifest 明确 `subset=true`、预期数量 1，不代替完整网格。两次同配置、代码和环境的 PNG 与规范化真值 hash 应一致；不要求不同 OpenCV 版本的 PNG 字节一致。内部 `verify` 还会从描述重建全部真值、重编码 PNG 并比较 hash。

## 输出和失败定位

`runs/<run-id>/` 包含 manifest、effective_config、index、truth、灰度 images、二值 masks、uint16 instance_masks、只有列头的 observation_template、verification。`plan` 目录包含完整 planned_cases 清单和规划 manifest，没有图片。运行结束打印绝对产物路径。详情见 [数据协议](docs/fixture_protocol.md) 与 [来源映射](docs/source_mapping.md)。

生成事务一直保留 `INCOMPLETE`，直到文件写完且内部独立校验通过才写 `COMPLETE`。异常保存 failure 和已成功写入的样例数；强制中断留不完整状态，不自动恢复、不覆盖。公开 verify 只读已有数据文件，仅新增/更新 `verification.json` 报告；缺文件、错 hash、漏项、未知额外图像都会失败，`algorithm_validation` 始终为 `NOT_RUN`。失败后用新目录重新生成。

| 问题 | 报错位置/先看哪里 | 下一步 |
|---|---|---|
| 找不到编译器/CMake | 终端 `command not found` 或配置错误 | 安装 build-essential/cmake，确认 PATH，再配置 |
| 找不到 OpenCV/nlohmann_json/OpenSSL | CMake 的 `find_package` 错误，build/CMakeFiles/CMakeConfigureLog.yaml | 安装对应开发包；非系统前缀需配置 CMAKE_PREFIX_PATH |
| 工作目录错误 | 退出码2，终端提示工具根目录 | 回到本 README 所在目录执行二进制 |
| 缺输入 | 退出码2，终端含 inputs 路径 | 放回交付原件与 sources，不从其他仓库取替代件 |
| JSON 缺字段/类型/来源 hash 错 | 退出码2，终端中的文件/字段，inputs/sources.json 与 configs | 对照来源原文与当前私有 JSON；实验约定不支持删改网格后冒充覆盖 |
| 缺批准/批准 hash 不匹配 | formal 退出码3 `APPROVAL_REQUIRED`，inputs/approval.json；启动前无run | 查看 validate 打印的 hash，与真实用户确认版本核对；等待有效记录 |
| run 目录已存在 | 退出码4，终端指定 runs 路径 | 选择全新 run-id，旧目录保留 |
| 空间不足/无写权限 | 退出码4，终端 I/O 路径；已创建 run 的 manifest.failure | 查看磁盘空间和工具目录权限；换新run重试，不能把 INCOMPLETE 当完成 |
| CTest 失败 | 失败测试名与所用构建目录的 Testing/Temporary/LastTest.log | 按 model/transforms/grid/raster/perturb/cases/workflow 定位模块 |
| verify 缺文件或 hash 错 | 退出码4，runs/<id>/verification.json 的 failures/failure_ids | 按 case_id 与相对路径定位；保留损坏证据并生成新的run |
| verify 环境/代码不同 | verification 的环境/代码版本错误 | 使用原二进制及依赖环境，或在新目录重生成并保留新manifest |

退出码：0 成功；2 输入/配置无效；3 正式资格缺批准；4 I/O 或产物校验失败。报错走 stderr；启动前错误明确没有创建运行目录，已有运行的失败保存在清单或验证报告。工具不接触视频、baseline、作业仓库构建配置及全局 Git/依赖设置。

本轮验收实际使用 GNU 13.3.0、CMake 3.28.3、Release/C++17、OpenCV 4.6.0、nlohmann_json 3.11.3、OpenSSL 3.0.13。最终从全新 `build-acceptance/` 复核，记录与39分30秒墙钟耗时见 [acceptance](docs/acceptance.md)；原始 build 的二进制仍在 `build/synthetic_gen`。正式数据因缺批准尚未交付，外部算法验收未运行。

现行步骤声明分别由 model/grid/transforms/perturb/cases/raster/records/verify 头拥有，core.hpp 只作兼容聚合。连续场景与真值编码内部只接收所需配置切片、坐标、场景及栅格；公开 Context 和入口保持。测试显式提供来源／证据根，load_context(source_root) 可直接指定输入根；旧 CLI 的零参入口仍使用工具根工作目录。workflow 的临时目录切换有局部恢复，异常不串用下一测试。
