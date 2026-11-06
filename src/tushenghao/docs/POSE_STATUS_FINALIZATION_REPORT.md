# G2位姿状态与输出收尾报告

日期：2026-11-06；分支`refactor/vision-lean-fixed`，起点`a392bd4`，开始时工作区干净。main、refactor/lean、refactor/vision-lean未修改。当前执行细节见原位更新的[flow-guide](flow-guide.md)，统计与复现见[验证材料](validation/pose-status/README.md)。

## 结论与最后保留的状态

Unique/Ambiguous在起点代码中**没有行为区别**：G4对两者同样接收；滤波、准入、检测、拟合没有按这两个状态分路。差别在状态名称、叠加显示、统计和依赖旧名称的测试。但G4确实消费另一支解的位置分歧，不能把多解的数值信息当死标签删掉。

最后只保留**两种结果**：

- `estimate_`有值：有数学合法的名义估计，显示/摘要为`ESTIMATED`。
- `estimate_`无值：没有估计，显示/摘要为`UNAVAILABLE`。

C++不再存PoseStatus枚举、成功布尔或选中索引；可选结果本身是唯一的可用性表示。输入错误/无数学合法解保留已有短reason诊断；正常未检出返回空estimate。没有再存一组InvalidInput/NotDetected/NoSolution失败分类，也没有用新名称保留Unique-only门。Detector的输入/检测状态和G4的Measured/Coasting仍有实际不同语义与行为，保留。

本次完整1676帧回放：**1088 ESTIMATED、588 UNAVAILABLE，1088次检测、4352角**。相对a392bd4，全部名义rvec、平移、RMSE逐值完全一致，G4整份轨迹文件逐字节一致；只改变接口/呈现，不改变有效测量、精度和跟踪行为。

## G2是不是必然两解

分清三个层次：

1. OpenCV 4.6的IPPE正常求解路径产生两支候选并加入结果数组；该路径也处理求解异常，不能把正常路径当所有输入的承诺。[OpenCV 4.6实现](https://github.com/opencv/opencv/blob/4.6.0/modules/calib3d/src/solvepnp.cpp)
2. 本项目还复核有限性、正深度和可重投影；非法/数值退化输入可能没有合法解，某支复核失败也不能强行补成两支。内部按实际合法集合计算，不固定访问第0/1支，不把候选计数当物理唯一性的证明。[PnP多解接口说明](https://docs.opencv.org/4.6.0/d5/d1f/calib3d_solvePnP.html)
3. **新公共G2接口输出0或1个名义估计，而不是输出两解列表。**多解只在G2内部处理，其位置分歧通过干净数值字段给G4。

只读测试工具`ippe_solution_count`使用实际C++ OpenCV **4.6.0**、公开Detector和与G2相同的IPPE/数学复核重新扫完整录像，非生产trace：

| 层次 | 本片分布 |
|---|---|
| 原始IPPE结果数 | 1088次检测全部为2，共2176支 |
| 数学复核后的合法结果数 | 同样1088次全部为2 |
| 没有检测的帧 | 588，不进入该MARK求解 |
| 对下游的名义估计数 | 1088帧各1个，588帧为空 |

本片没有合法结果数为1的样本，不伪造该分支的实拍覆盖。单支情况下S=0的接口由实现自然支持，G4零分歧输入有活跃测试。两个数值候选也不必代表两个可区分的物理解；因此不再以计数输出“Unique”承诺。

## 接口与解耦

`PoseObservation`只给可选`PoseEstimate`及帧/来源/目标/检测索引、必要故障原因。估计内只有名义位姿、该支RMSE和`translation_spread_m2_`。

G2内部仍保留所有数学合法IPPE候选与原来的最低RMSE选支。把原来G4中的连续权重计算**原样移到G2内部**：

`S = Σ [e0²/(e0²+ej²)] (tj−t0)(tj−t0)^T`。

两误差都0时权重1/2；先归一化再平方，保持原数值计算。S是相对名义平移的加权二阶矩；不是经过标定的后验概率或完整SE3协方差。一合法支时S=0；平移相同、旋转不同也可能S=0，不能凭零矩阵证明姿态唯一。

G4只消费可选估计、名义平移、S和身份/时间事实，直接用`R_eff=R_profile+S`。不再了解候选数组、选中索引、成功分类或RMSE权重算法。G4保留实际消费的位置/矩阵有限性与原滤波器数学保护，删除不再消费的RMSE合法性门。G2自己的有限RMSE复核仍保留。

**G3只消费G4输出**：Measured/Coasting、位置、可选速度、协方差P、末次实测时间/当前状态时间和轨迹ID。有轨迹即有位置和P，只有速度在首样本可空。S已通过R_eff进入P，不需要G3回头读G2。当前没有独立歧义bit/分数的G3消费者，因此不新造标签或回传候选；P表达位置状态的不确定度，不冒充完整旋转歧义信息。

## 清理了哪些同类冗余，文件放在哪

| 清理 | 位置与结果 |
|---|---|
| 公共候选/索引/状态+getter | [pose_types.hpp](../include/vision/types/pose_types.hpp)：删除PoseStatus、candidates_、selected_index_和measurement()；可选estimate是唯一事实 |
| 候选类型暴露 | [pose_geometry.hpp](../src/pose/validation/pose_geometry.hpp)：PoseCandidate收为G2私有类型，只有内部求解/数学复核使用 |
| 选支与分歧计算责任 | [pose_estimator.cpp](../src/pose/estimation/pose_estimator.cpp)：G2内部选名义支并汇总S，无新质量阈值 |
| G4多解知识/重复准入 | [measurement_adapter.cpp](../src/tracking/measurement/measurement_adapter.cpp)：删除状态两值并集、候选遍历/再次校验和权重计算，只接位置/S |
| 输出的重复位姿 | [pose_io.cpp](../src/pose/io/pose_io.cpp)：不再同时写候选、索引和顶层名义rvec/t；只有一个estimate对象 |
| 多份状态文案 | 类型头中的一个`pose_status_name(observation)`按optional生成两值，应用/序列化/叠加共用；删除几何cpp中的旧枚举名称表 |
| 头文件依赖 | 公开位姿类型不再为候选列表引入vector；Tag/G4自己的vector直接包含自己的依赖。私有几何复核不再包含无用pose_config |
| G4完整状态的重复空值包装 | Lost清空已删除，实际发布的每条轨迹必有position/P；改为必填字段，删除writer/renderer的恒真空值分支。速度仍可空，无轨迹仍为空列表 |
| 构建脚本 | [pose/CMakeLists](../src/pose/CMakeLists.txt)删重复G2 option、失效pose_app/cmake/g2.cmake说明，合并重复if；测试include放回所属测试块 |
| 依赖测试/回放 | schema6测试reader不再还原候选/索引；应用、退出Tag、契约测试和比较器同步改为可选估计 |

不新增文件层级/通用包装。状态呈现放回实际类型旁，候选留在现有私有validation头；求解、几何复核、IO、跟踪adapter职责仍分开，不为省文件把不同职责合并。

自查后保留：Detector非法输入与未检出（入口/上下文处理确实不同）、G4 Measured/Coasting（更新与预测确实不同）、真实时间/身份/固定映射、噪声R/q/vv及位置/速度/P。这些有实际算法消费者，不当作显示标签删掉。没有恢复fit trace、配置写回或其他生产测试钩子。

## 构建、测试与录像对照

Release，实际求解器C++ OpenCV 4.6.0。完整链CTest **11/11**，G2版 **9/9**，纯检测 **7/7**，Tools=OFF/Testing=OFF纯库构建成功；最终日志无warning/error。仅检测构建也通过，证明显示名称不再依赖未构建G2库中的状态函数。

活跃测试覆盖：名义估计、非零分歧、空估计、NaN输入/负深度保护、退出Tag未知尺度、schema6无候选/索引/重复顶层位姿、G4接收S/零S、仅实际消费字段的有限保护、全链22次更新/2次coast、原拟合与结构反例。fixture输入不伪造求解器候选，G4单元测试直接构造自己的位置/S输入。

完整应用各跑一次默认空profile与条件profile，均为1676帧。条件profile沿用上一轮相同合成R/q/vv，没有改滤波参数或填入生产profile。

| 对照 | a392bd4 | 当前 |
|---|---|---|
| 检出/精角 | 1088/4352 | 相同，RAW逐字节一致 |
| 名义rvec、平移、RMSE | 1088份 | 全部逐值相等 |
| 位置分歧S | 原G4在内部计算 | 新G2字段逐项等于原公式 |
| PnP RMSE中位/P95/最大(px) | .3575726633/.6906709467/.9263200336 | 完全一致 |
| 呈现状态 | 1088 AMBIGUOUS、588 NOT_DETECTED | 1088 ESTIMATED、588 UNAVAILABLE |
| G4 Measured/Coasting | 1088/588 | 相同 |
| 位置/P、速度帧数 | 1676、1675 | 相同 |
| G4轨迹ID数 | 1 | 相同 |
| G4整份输出（含位置/速度/P/NIS/时间/事件） | 基线归档 | **逐字节一致** |

另确认：两种profile下的观测文件逐字节一致；schema6测试reader调用真实G4所得tracks与应用输出也逐字节一致。没有用脚本重新实现滤波来代替应用重验。新绘制状态文案会改变overlay像素，这是预期显示变化；MJPG AVI独立完整解码仍是1440×1080、1676帧。

默认生产profile仍为空，故条件G4证据不等于本片/板端真实噪声标定；本轮证明相对基线数值和行为保持，不新增绝对角点/真实速度精度承诺。没有板端录像，之前MaixCam待验项不变。

## 交付与复现

[validation/pose-status](validation/pose-status/README.md)保存新schema6观测、逐帧IPPE计数、两次摘要、构建/测试/运行日志、比较结果、校验值和复现步骤。RAW/条件tracks与上一轮完全相同，复用[原归档](validation/single-candidate/README.md)，不再复制相同大文件；检查清单记录新输出的独立SHA256和相等结果。

生产观测文件schema **5→6**，轨迹仍schema2、配置仍schema2。当前测试回放reader只读6，历史比较器在测试侧读取5作基线；没有在生产加入旧格式兼容支路。旧156项报告/落实表标为a392bd4快照，落实表源码定位固定到该提交，当前README/规则/索引/flow已同步。
