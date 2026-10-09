# 112 恢复施工记录

状态：完成，全部实现与验收通过，证据已封存，本轮专用build已清理；分支 `fix/112-recovery`，起点与 main 相同，为 `4ad0a349cf5ee5bf13c2c35256e8beb4d3810d7c`。
依据：[执行提示词](ref/codex-112-implementation-prompt.md)、[实施主干](evidence/112-sandbox/EXECUTION_PLAN.md)、[性能主干](evidence/112-worstframe/REPORT.md)。生产实现从现有 main 代码重写，没有复制沙盒实现或环境开关。

## 执行顺序

0. 保护公共头、YAML、ref 和原 README；原 README 已完整复制到 [历史全文](history/README_before_112_recovery.md)。专用构建目录为 `build/112-recovery/`，用户已有 `build/release` 保留。记录实际环境、源码与输入 SHA256。
1. 原版 Release 25/25 回归通过；公共 Detector 单线程顺序运行全部 1676 帧，976 检测。均值 78.899ms，P50/P95/P99 为 81.113/199.549/262.748ms，最大 345.681ms。数据见 [基线](evidence/112-recovery/baseline/summary.json)。
2. 帧内原图索引独立验收通过：全部 1676 帧状态/raw 四角差异为 0，976 检测，Release 25/25；[索引验收](evidence/112-recovery/step1-index/summary.json)。
3. 精确等价弧对优化独立验收通过：全部 1676 帧状态/raw 四角差异为 0，976 检测，Release 25/25。均值 21.640ms，最大 94.849ms；[搜索优化验收](evidence/112-recovery/step2-search/summary.json)。
4. A/B 失败补查独立验收通过：996 检测，指定集合新增 20、旧损失/旧 raw 变化/集合外新增均为 0，Release 25/25；[A/B 验收](evidence/112-recovery/step3-ab/summary.json)。
5. C1 相位中位+最后 L1 路由整链通过：1088 检测，112/112 恢复，旧 raw 变化/集合外新增均为 0，Release 25/25；[周期模型验收](evidence/112-recovery/step4-periodic/summary.json)。均值 34.750ms，最大 2685.656ms，尚不满足性能目标。
6. 证据契约验收通过：已知参数/轴对称/满秩检查、6 类篡改主动检查；固定校准 24/25、固定测试 67/68、20/20 负样本拒绝。通过测试的最大真值误差 1.401158px，11/25 次实际周期参数篡改分别全部拒绝。固定采样 ID/角度/幅度/局部模式/负样本逐项与旧封存记录相同；[证据身份](evidence/112-recovery/step5-manifest.json)。
7. 调用内拟合、支持描述和纯几何配对缓存、front 桶、固定 8 槽计数与快速路线已实现。39 组完整 reference 对账中全部合格弧、配对、顺序与最终证据逐值一致。首次全视频均值 9.165ms、最大 39.866ms、超 33ms 为 3；复用描述构造短向量、预留支持容量与延迟短支持 materialization 后均值 7.488ms、最大 34.050ms、超 33ms 为 1。两个未达标运行完整保留。周期估计器改为显式局部 scratch 复用坐标/相位向量容量后，均值 7.370ms、最大 27.759ms、超 33ms 为 0，所有 raw 不变。
8. 最后审校 validator：Legacy 同样从全部支持重建 cv::fitLine，所有模型都复算两类残差，Legacy 轴/c 必须默认；没有改原门限。Release/启用断言 Debug 各25/25；固定独立测试再次67/68、20/20负样本拒绝、25次周期篡改全部拒绝、最大真值误差1.401158px。
9. 最终冻结代码顺序复测两次：均值7.673/7.484ms，P50为8.583/8.412、P95为16.131/15.577、P99为21.398/20.454、最大27.803/26.063ms，超33ms均0；每次完整1676帧，1088检测、112恢复、旧raw变化0、集合外新增0，两次raw彼此全等。未与编译、回归或合成并发；[独立统一核查](evidence/112-recovery/summary.json)。
10. 17个C++文件clang-format检查通过，公共头/配置/ref及原README字节保护通过。归档最终源码、构建flags/CTest原日志和二进制哈希后删除本轮专用build；用户原build/release保留。[清理记录](evidence/112-recovery/cleanup.json)。

## 代码阅读路线

`corner_observation.hpp/.cpp` 的索引绑定当前只读 PreparedFrame、明确帧号和预算。`decode_stage.cpp` 在 decode 中创建局部对象，`corner_resolver.cpp` 内部重载显式接收它。原 wrapper 每次重新创建索引，避免独立 fixture 改图或改预算后读到历史缓存。

灰度、阈值和完整原图 findContours 惰性执行一次，双向来源关联按 component ID 缓存。原来源唯一性、边界截断判断和原始连续轮廓均保留。新增主动回归检查重复调用命中同一缓存、同 ID 换黑图重新拒绝和帧号错配拒绝。

`corner_edge_fit.cpp` 的等价优化先将两个指定有限边的逐像素位置判据缓存为 mask；沿弧 AND 为零即可拒绝，不再重复算距离。坐标到索引的缓存只保留 legacy find 的首次语义；支持路径本身不按坐标合并。循环长度前缀的舍入缓冲为 `64*epsilon*N*max(1,totalLength)`，近预算仍按原顺序求和。AABB 不相交时跳过共享像素查询；全部质量谓词通过后每个候选仍执行歧义检查，只有更优候选才复制完整取证序列。

A/B 使用拟合实例内策略，不读取环境变量：旧候选/原位置成功直接返回，失败后 B 使用原端点+投影后位置，A 使用全部真实端点+原位置。后验逐点投影到实测物理线，位置仍对指定有限边检查 9.0px；全端点按完整有序像素序列去重。

`periodic_line_fit.hpp/.cpp` 是纯估计器：中心化充分统计量解 OLS；固定斜率的两相位中位数重建 b/c；最后失败才做 40 次凸区间搜索及同相位整数点对拐点复算，复用局部两相位 scratch。保留严格零斜率 L1 下界证书。零斜率类型 5 是同一模型的 a=0 子空间，独立具名，不绑定任何帧号。

`corner_types.hpp` 只增加内部模型/轴/c/校正残差元数据。`corner_evidence_validation.cpp` 从完整有序支持重建周期参数与物理线、分别复算 raw 和校正误差。旧 `line_mean_residual_px_` 等字段与 `error_` 的排序定义不变。模型平均垂距 0.5px 的量纲依据是整数格点正交坐标量化上界；支持仍保留全部访问，不能用删点或改权重满足门控。

周期纯函数有独立 wrapper 和显式 scratch 重载：正式拟合实例只复用向量容量，每条支持仍清空后从头计算 a/b/c；validator 的 wrapper 用独立局部 scratch，不读取拟合缓存。旧线也独立重建参数，不信任调用方仅自述低误差。

最终 `corner_edge_fit.cpp` 的阅读顺序是 prepareContour → excludedAt/describe/descriptors → supportFor/fitFor → matches → pairGeometry → select → stage/run。端点剔除邻域惰性扫描完整原轮廓，仍用原 `cv::norm < turn_trim`；描述剥掉首尾剔除段，中间有剔除点严格判不连续。拟合缓存按模型和 raw 起止索引共享成功/失败，原端点别名保留 begin/end 和 tie-break。AABB 无交集跳过共享像素全集，否则按完整集合精确检查；唯一点数达到下限即停，但拟合使用全部像素。

front 桶只排除沿原轮廓连接明显超预算的配对，候选 j 恢复原顺序；近边界继续原逐段求和。每搜索阶段的纯几何 memo 不含 best，缓存成功后每个别名仍执行歧义检查再评分。计数改固定 8 槽，非零原因按字典序输出；索引提前排除的必要条件可能改变拒绝计数分类，状态、候选和取证是回归契约。

完整枚举 reference 保留在 `tests/support/recovery112_reference.cpp`，来自本轮逐步验收的重写代码，仅测试链接。`corner_edge_fit_test.cpp` 比较矩形、圆角、回走尖刺、奇偶扰动及反向轮廓，另覆盖连接预算前后一个 ULP 和不同 trim；逐项比较全部合格弧、通过质量谓词的配对与最终证据，不只比较最终角点。

## 执行中的问题

clang-format 未安装。第一次仅写 /tmp 的安装因沙箱网络限制失败，经工具授权后安装 18.1.8；生产依赖不变。两次安装日志均归档。

证据测试最初使用轴向奇偶矩形，其链码可被旧单线刚好接受；因此“必须实际使用周期模型”的测试前提失败。改用固定 2° 旋转的独立图纸扰动，检查确实用到周期且 raw>0.5 的证据。只修正测试前提，未改算法或预算；首次失败日志保留于 `step5-ctest-first.log`。

## 证据与边界

每次完整视频运行直接保存 JSONL、运行日志、源码快照、输入/二进制 SHA256 和 manifest 到 [112-recovery](evidence/112-recovery/)。全部旧成功 raw 四角按 JSON 双精度数值逐值比较，计时分位数采用最近秩，不删慢帧。
最终数据见 [验收表](fix112_recovery_acceptance.md)。README同时标明历史沙盒7.5/29.5ms参考和本次7.673/7.484、27.803/26.063ms实测，没有冒用旧计时。

快速优先级改变21个已恢复帧的角点，最大相对第四步旧顺序变化1.628526px；这是两解差，不能当作实拍真值误差。全部1088输出与历史快速路线也逐值一致，原976不变。2px压力盲区、固定1676帧范围、无实拍亚像素真值、新增检测可改变平滑历史等限制保留；不声称任意输入/硬件都在33ms内。

源码快照、输入/配置/工具/构建SHA256、全部归档清单见 [manifest](evidence/112-recovery/manifest.json)。只清理自建 `build/112-recovery/`；构建参数、CTest原始日志与二进制身份先归档，用户已有 `build/release` 保留。
