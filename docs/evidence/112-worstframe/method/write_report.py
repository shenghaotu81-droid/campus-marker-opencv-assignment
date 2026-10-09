# 中文用途：从已完成对账的数据生成完整报告和实施方案第5节，历史封存报告另存before，不改写旧原始运行。
import json
from pathlib import Path
root=Path('docs/evidence/112-worstframe');s=json.loads((root/'summary.json').read_text())
rows=[('frozen-repeat','上一轮冻结库本轮复测'),('zero-index','零斜率候选+连接索引（探索）'),('cache-profile','支持拟合复用（含profiling，探索）'),('ranges-first','连续支持索引（探索）'),('descriptors-first','描述复用/延迟集合（探索）'),('priority-first','周期模型优先（探索）'),('pair-cache-final-repeat1','配对缓存，数组计数前复测1'),('pair-cache-final-repeat2','配对缓存，数组计数前复测2'),('exact-final','最终代码，保持原搜索优先级'),('final-repeat1','最终优先路线复测1'),('final-repeat2','最终优先路线复测2'),('final-profile','最终优先路线profiling')]
table='| 完整1676帧版本 | 均值 ms | P50 | P95 | P99 | 最大 ms | 超33ms帧数 | 帧153 ms |\n|---|---:|---:|---:|---:|---:|---:|---:|\n'
for n,label in rows:
 r=s[n]['elapsed_ms'];table+=f'| {label} | {r["mean"]:.3f} | {r["p50"]:.3f} | {r["p95"]:.3f} | {r["p99"]:.3f} | {r["max"]:.3f} | {r["over33"]} | {r["frame153"]:.3f} |\n'
changed=s['final-repeat2']['changed_vs_frozen'];delta=max(r['max_corner_delta'] for r in changed)
profile='| 最终函数（inclusive） | 均值 ms/frame |\n|---|---:|\n'
for k,v in s['final-profile']['functions_mean_ms'].items():profile+=f'| {k} | {v:.3f} |\n'
ind='| 固定独立分区 | 样本 | 正样本通过/总数 | 通过样本最大真值误差px | 负样本错误接受 | 实际参数篡改检查 |\n|---|---:|---:|---:|---:|---:|\n'
for n,label in [('independent-calibration','校准'),('independent-test','测试')]:
 r=s['independent'][n];ind+=f'| {label} | {r["samples"]} | {r["accepted"]}/{r["positive"]} | {r["max_truth_error"]:.6f} | 0/{r["negative"]} | {r["tamper_checked"]}次，全部拒绝 |\n'
section=f'''## 5. 性能与最慢帧：本轮更新后的正式结果

**硬目标在固定完整视频上已达成：两次顺序复测均值7.526/7.599 ms，最大29.513/29.250 ms；每次全部1676帧，超33ms为0，112/112恢复保持。** 这是当前CPU/配置下公共 Detector::process 的实测，不是任意硬件、任意输入或摄像解码/GUI整体链的实时SLA。

本节取代上一版约34ms均值/约5秒最慢帧的性能结论。原封存报告与manifest已原样保存到 `../112-worstframe/before/`；旧manifest对应旧报告，更新报告由新目录的manifest封存。其他章节的早期实验数值/旧路由作为历史步骤保留；要达到本节目标，main必须追加下述索引、缓存、候选顺序和验证改动，不能只实施第3节的旧失败路由。

### 5.1 计时范围和完整分布

环境仍为Intel Core i9-14900HX、WSL/Microsoft hypervisor、OpenCV4.6.0、Release `-O3 -DNDEBUG`，`cv::setNumThreads(1)`；没有增加工作线程、CPU绑核、快数学编译或调高优先级。计时仍为公共process的steady_clock，包含预处理、几何、解码、稳定、Summary诊断和本轮缓存的创建/释放；VideoCapture、JSON stdout、GUI/导出仍在原范围外。没有删慢帧，没有把partial/异常退出日志作为完整样本，没有将profiling时间冒充无插桩结果。

探索版部分运行与构建重叠，仅用来定位大幅成本变化；正式冻结复测1→复测2→profiling→原优先级对照→原算法结果对照按顺序执行，未与编译/其他视频或拟合实验并发。随后才运行合成/回归与渲染。没有根据最终复测结果换CPU、挑子集或改测试集。分位数统一采用最近秩 `ceil(p*N)-1`。

{table}

原代码基线仍是上一轮未修改源码的976检测库，不能把已经优化的baseline路由当成原性能基线。本轮 `frozen-repeat` 使用上一轮冻结的1088检测二进制重新完整计时，所有raw输出与上一轮逐值相同。各探索版及所有正式版都保持1088检测；`optimized-baseline`关闭A/B/C后严格保持原976检测的1676帧status/raw全等。

### 5.2 最慢帧的算法原因与实际消除方式

新的源码核对澄清提示词中的前提：**上一轮最终冻结源码已经包含斜率拐点去重及严格零斜率L1下界证书**；`lad-dedup-exploratory`只是较早的探索快照。5秒不能再归因于“冻结版没有去重”。

帧153的物理线仍是x=1135.5和y=517，垂直周期c=0.5。上轮L1精化选择24点支持，raw/校正平均残差0.791667/0.416667；本轮原端点零斜率候选选择另一条26点连续支持，raw/校正平均残差0.846154/0.500000，严格按既有≤0.5门控通过。两者物理角相同，不能把旧支持的0.416667冒充新支持误差，完整像素见selected-evidence。旧搜索先失败多轮原/后验位置/全端点/相位中位，再对大量候选逐条做80次目标求值和点对拐点检查；并非最后那一条正确弧本身需要5秒。新路线先验证原端点上的合法周期候选和固定零斜率候选，该帧在L1穷举前已得到同一物理角。L1精化仍保留供真的失败输入补查，没有超时、帧号分支或强制early reject。

零斜率候选属于同一个三参数模型的a=0子空间：两相位原正交坐标各取中位数，b为两者均值、c为半差，保留所有串行像素与原权重。它不宣称一定是全局L1最优，不删除不利像素，也不使用父仿射/历史角。支持跨度、点数、校正平均垂距0.5、有限边9.0、方向、转折、延伸、交角、歧义和生产validator仍全部执行；模型类型5必须独立重建参数。0.5的量仍是第6节已批准的周期校正平均垂距，定义未变。

仅给153加快速候选仍使最大约528ms；复用拟合约361ms；按真实修剪索引生成支持及复用描述表后约115ms。优先在原端点验证周期模型后最大约44ms；热点转移到7/14/382/395/620等帧的重复弧对。帧14在配对缓存前profile约40.12ms fit，其中约22.63ms pairs，单改L1迭代次数不能解决它。

配对缓存进一步降到约33–37ms，但仍未达硬目标，不能将33.006ms四舍五入冒称达标。最后把热点中每次失败的字符串构造/红黑树计数替换为固定8槽计数器，失败原因的数值和字典序输出保留；没有删除诊断或改变计时范围。最终两次全部帧低于33ms。这里的瓶颈主要是重复候选计算和工程数据结构，实验证明原5秒并非物理模型或相机输入的不可避免下限。

### 5.3 main上必须追加的具体实施

1. `corner_edge_fit.cpp`内部增加一次fit调用范围的scratch。按 `(modelKind, firstRawIndex, lastRawIndex)` 缓存拟合结果，包括成功/失败、完整有序支持、物理线、raw/模型残差和AABB；不按端点坐标合并不同路径。所有原端点别名保留其begin/end、枚举位置与tie-break。缓存仅在不可变contour/配置/两条有限模型边的该次调用内有效，不跨帧、组件或父假设复用。
2. 惰性计算每个端点在**完整轮廓**上的欧氏剔除邻域，采用与原代码完全相同的`cv::norm < turn_trim`，不是沿轮廓距离、平方距离近似或只看邻近索引。将剔除位置换算为当前区间的有向offset，剥去首尾剔除段；中间还有剔除点则严格判不连续。分别缓存原端点/全端点的支持描述，保持原枚举顺序。两条有限边的原像素位置mask用前缀坏点计数作同一谓词的区间AND；后验位置模式继续检查投影位置，不能误用原像素必要条件拒绝它。
3. 唯一点数检查只证明达到原min_line_points下限，达到即停止收集去重计数；实际拟合/残差仍使用全部像素。完整共享像素集合只在AABB重叠确实需要时构建，并在同一支持别名之间共享，构建后不再改内容。共享状态仅调用内单线程使用，不能直接作为并行安全结构。支持materialization延迟到确需拟合/匹配/完整序列去重时。全端点序列去重保留，先用严格相同原始range排重复，再用完整有序像素序列排不同range的相同序列。
4. 按支持的legacy首次坐标索引建立front桶；对每条a，只列举正向原轮廓连接长度可能在既有预算内的桶，仍用旧保守舍入界。候选j排序回原索引顺序；近边界仍逐段复算真实连接。不能把几何附近的任意边当成拓扑相邻。
5. 每个搜索阶段以有序的两条拟合支持身份缓存纯几何配对结果：共享像素、真实连接、凸性、交角、交点、转折误差、延伸。该阶段相同拟合支持匹配身份一致。缓存失败仍按原次数计入同一拒绝原因；缓存成功**每个别名仍无条件执行歧义检查，再执行评分/tie-break**。不能因有缓存就跳过低分候选的歧义，也不能把当前best或是否发表放进几何memo。
6. 固定8槽拒绝计数替代热循环字符串map；最终失败原因按原字典序，仅输出非零项。整数像素奇偶判定用精确转换代替libm llround；非整数/越界仍走原llround。原像素位置mask已证明通过时不再次扫描同一原位置谓词；严格轴向单位线的extension用AABB极值原式计算，其他方向继续原逐点算法。这些都是计算复用，不能套用误差容差放宽质量。
7. `sandbox_periodic.hpp`中的零斜率估计在main内部建成纯函数；`corner_evidence_validation.cpp`增对应模型类型，独立重算全部参数和两类残差。本轮public接口和Detection不变，YAML不变。
8. 原候选成功仍最先返回。原失败后，先在**原端点**依次验证：相位中位+原位置、零斜率+原位置、相位中位+后验位置、零斜率+后验位置；失败后执行原完整B/A/C/L1补查。main以实例内策略表达该顺序，不能复制`MARK_WORST_ROUTE=phase_first`实验环境开关。仅保留全部等价缓存而不改变此顺序，虽raw逐值全等，最慢仍98ms，不能作为满足本硬目标的交付。

### 5.4 正确性变化必须明确呈现

最终所有1676帧的检测状态与上一轮冻结库相同，原976成功帧的raw四角逐值不变，112恢复保持，集合外新增0，两次新路线raw输出彼此逐值相同。新优先级改变了{len(changed)}个**已恢复帧**的raw角点，最大相对旧解变化{delta:.6f}px；这是解与解之差，不是相对实拍真值误差。ID与逐帧delta完整保存在 `../112-worstframe/summary.json`。不能把新路线声称为所有1088角点都与旧库逐值一致。

保留原搜索优先级的 `exact-final` 则所有1088输出与冻结库全等，但最大98.074ms、超33ms为57帧。二者差别来自先验证的合法模型/支持，不是去重或浮点计算偷偷改变原成功结果。新路线每个新增模型仍由当前原图连续支持、有限关联、真实凸转折和独立validator支持。全部112新叠图已目视核对，未见选择内片/空背景；不能用叠图替代实拍亚像素真值。

{ind}

独立生成器源码SHA256与上一轮sealed manifest相同；样本ID、种子、角度、幅度、局部/全局模式及负样本类型逐项对账相同。没有为本轮速度改测试集。校准和测试仅各自固定2px压力ID2继续严格拒绝，67个0/1px正测试全部通过、20个负样本全部拒绝，真实执行的25次周期参数篡改全部拒绝。Release/Debug、原路径/优先路线四份CTest日志均25/25通过，Debug断言启用。

### 5.5 最终profiling与结论边界

{profile}

arc_fit包含在arc_generation，arc_generation/pairs包含在fit；gray/threshold/contours/mapping包含在observe，不纵向重复相加。原上轮profile的fit约71.9ms、pairs约35.9ms，是未优化原算法，不与当前冻结1088库的35ms总process混为同一计时。

本轮达到了**固定1676帧基准**的max≤33ms、mean≤25ms，不需要降低目标或剔除输入，也没有病态帧超时丢弃。继续保留完整fallback意味着任意新图像的候选规模仍可增大，特别是独立2px压力和极复杂轮廓；本轮没有证明所有可能输入都具有33ms最坏界。若将来要求跨所有输入/硬件的硬实时SLA，仍需约束候选复杂度、采用更多连续支持/几何索引，并处理OS调度及内存分配；不能用本有限实验替代这样的算法/系统证明。

复现、阶段完整数据、源码快照和SHA256见 `../112-worstframe/REPORT.md`、`REPRODUCE.md`及`manifest.json`。本轮源码仍保留在同一沙盒分支，未commit/push、未碰main，任务到此收敛。

'''
p=Path('docs/evidence/112-sandbox/EXECUTION_PLAN.md');old=p.read_text();a=old.index('## 5. ');b=old.index('## 6. ',a);updated=old[:a]+section+old[b:]
updated=updated.replace('帧 153 最终 P2 使用垂直物理线','上一轮冻结版本的帧 153 P2 使用垂直物理线')
p.write_text(updated)
(root/'REPORT.md').write_text('# 最慢帧深入沙盒实验报告\n\n本报告来自逐帧对账完成后的实际数据。原任务见 `src/tushenghao/docs/ref/codex-112-worstframe-prompt.md`。实现与正确性说明、全部分布和结论如下；没有只报告最好的一个试跑。\n\n'+section+'\n## 证据索引\n\n- `summary.json`、`validation-summary.log`：所有完整版本、逐帧状态/四角/变化、分位数、profile、独立试验、CTest对账。\n- `final-repeat1/2.jsonl`：最终两次完整无profiling运行；`final-profile.jsonl`：完整函数计时。\n- `frozen-repeat.jsonl`：上一轮冻结二进制本轮完整复测；`exact-final.jsonl`：保留原优先级全等对照。\n- `zero-index`、`cache-profile`、`ranges-*`、`descriptors-first`、`priority-first`、`pre-pair-*`、`pair-cache-*`：各实际中间路线，失败达标版本未删除。\n- `independent-*.jsonl`：同一固定25/88样本，全部结果；`ctest-*.log`：Release/Debug两条路线各25项。\n- `selected-evidence.jsonl`：153/382/784冻结原始输入的实际模型、完整支持与两类残差，局部拟合记录不冒称整链输出。\n- `images/`：全部112检测裁剪叠图及7张联系表；完整坐标仍以JSON和原视频为准。\n- `before/`：上一轮封存报告/manifest及阶段源码；`source-final/`、`sandbox.patch`、`worstframe.patch`：最终完整源码、相对HEAD和相对本轮开始的差异。\n- `method/`：方法正文；`REPRODUCE.md`：命令；`manifest.json`：全部源/证据/输入/二进制身份。\n\n旧 `docs/evidence/112-sandbox/manifest.json` 未重新生成或改写，它仍对应旧封存运行与源码快照；旧报告已搬存副本，新版实施报告hash由本轮manifest记录。实验源码保留，环境策略/计时全局状态仅供沙盒，main按设计重建。\n')
print('updated execution plan section5 and REPORT')
