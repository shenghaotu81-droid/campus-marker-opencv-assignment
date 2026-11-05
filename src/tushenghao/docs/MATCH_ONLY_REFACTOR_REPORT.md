# MARK match-only 与两路线拟合改动报告

> 后续状态：G2预算／消歧和视频容器已在a582c48之后更新，见[G-01/G-02报告](G01_G02_VALIDATION_REPORT.md)。本文的历史复跑数字和旧输出名保留原时点含义；现行执行顺序见[flow-guide](flow-guide.md)。

工作分支：`refactor/vision-lean`。比较基线：`b600635`（第一批交付）。本轮先提交规则草案 `16b72b0`，再按试验结果提交规则取舍 `e24568d`，之后实施代码（`1b3aff4`，随后`a01604a`清理未用周期损失计算）。最终规则见 [MATCH_ONLY_RULES.md](MATCH_ONLY_RULES.md)。

本轮完成 D05–D09 删除、直接四边形输入和两路线拟合。1676 帧的检出帧集合完全一致；相同 1088 帧的 PnP RMSE 中位、P95、最大值均降低。独立合成共同集的角点真值误差中位保持不变，P95、最大值降低。这里的结论限于下述验证集与统计口径，逐帧结果并非都改善，MaixCam 实拍尚未提供。

## 1. 范围与交付位置

本轮按用户后续授权实施原先延期的 D05–D09，以及建议书中的四边形直接给边、固定身份和完整性契约。保留第一批已经实施的质量标签、自检迁入测试等决定。本轮不把已改标签的残差、角误差、extension 变回拒绝门，也没有添加拟合质量拒绝预算。

用户本轮授权在仓库 `docs/` 写新规则和报告、整理此前任务文档，因此将此前根目录的 [SLIMMING_REPORT.md](SLIMMING_REPORT.md)、[CORNER_FIT_REFACTOR_PROPOSAL.md](CORNER_FIT_REFACTOR_PROPOSAL.md) 移到这里并标注历史状态。其他历史取证文档及其源快照未清理；它们不参与构建。flow-guide 仍在 [src/tushenghao/docs/flow-guide.md](flow-guide.md)，没有移到仓库根目录。`main`、`refactor/lean` 没有改动。

## 2. V1.0.3 规则核对

依据用户提供的 [V1.0.3 手册](/mnt/d/Personal/Study/26-27Fall/PNX/校内赛/规则与指南/PnX校内赛规则手册_V1.0.3.pdf) 和同目录 V1.0.2，逐页比较文本并核对图纸；两版各22个PDF页。以下PDF页码包含封面，印刷页码少1。

| 项目 | 核对结论 | 实现影响 |
|---|---|---|
| MARK | PDF15／印刷14，图3-7：**80×80mm**；L外臂30mm、笔画8mm，M角外臂14mm | PnP继续使用白色结构四角，米制边长0.08；匹配的30/14/80比例来自图纸 |
| 识别板 | PDF14／印刷13，图3-6：**115×120×15mm** | 外板不参与MARK角拟合或PnP尺度 |
| 后框／底座 | 后框201×128×187.5mm；底座接地外廓1420×160mm，高203mm标为参考 | 不能用底座外廓推定有效运动范围、投放口净宽或图像ROI |
| 安装 | §3.3.3：标记竖直、面向移动区域 | 保留一般透视、转动与画面位置变化，不强制图像直角／固定屏幕朝向 |
| 兑换 | PDF12／印刷11，§3.3.1：四档共用图案，官方信号切自动，时段1min，结束图案消失 | 当前帧匹配失败即未检出；不靠前帧补框，不用“看见MARK”判断控制授权 |
| 运动 | PDF16／印刷15，表3-2：固定；10s动/20s停；0.5m/s固定范围往复；0.5m/s随机范围往复 | 不假定固定周期／端点；0.5m/s不变成相机系速度拒绝门 |
| 退出 | 同节及图3-8：对正TAG36H11后后退，经结束RFID恢复控制 | 保留独立Tag分支；本轮不实现兑换控制。手册未给出退出Tag的ID／实物边长，现有空allowed_ids不等于退出功能已验证 |
| 设备 | PDF6／印刷5，物资MaixCam×1 | 当前是参考相机录像验证；不能据此宣称板端精度、延迟或曝光时间已验证 |

V1.0.3相对V1.0.2的实质文本变化：物资清单把“C610电机”更正为“C610电调”（PDF6）；新增自主设计制造机器人／部件、不得购买成品机器人或集成部件的制作规范（PDF18）。另有封面、版本表更新和一句跨页排版移动。视觉相关图纸、安装、兑换和运动规则没有变化；逐页嵌入图像字节哈希也一致。**没有需要修改MARK或识别板尺寸的版本变更。**

旧目录中基于V1.0.0的225提案使用过后框150×160×137.5mm、高235mm等旧值，不能覆盖最新图纸；这不是V1.0.2→V1.0.3的变化。

PDF SHA256：V1.0.2=`fc3490a3c507161e6f9bb1bb86b4e1623c5e7ca4e67b55ca7ea75068418d0bf1`；V1.0.3=`7ea9fe40e82befbc9163f13698a726751506379d6b79e405d020d600c4b79be7`。

## 3. 代码改动与模块边界

表内源码路径相对 `src/tushenghao/`。

| 改动 | 位置 | 最终行为 |
|---|---|---|
| 删除D05–D09 | `src/detection/geometry/{parent,validation,completion}`及旧L观察入口 | 删除三L排列、仿射拟合／验证、M/S投影匹配、有界绑定、hypothesis/batch/完整性枚举；没有把原搜索搬进拟合 |
| 直接匹配四边形 | [match_quad.cpp](../src/detection/geometry/matching/match_quad.cpp)、`contour_topology.hpp` | 当前白片局部归组、凸包实测顶点、四个不同角片、六边一凹的外角关系、三个长角＋一个短M；M固定P1，正绕序固定其余角 |
| 清理中间值 | `geometry_types.hpp`、`prepared_image.hpp` | WhiteComponent只剩轮廓、bbox、碰边；删除ShapeObservation、ComponentAssignment等类型和PreparedFrame内的旧组件适配字段 |
| 直接原图定位 | [corner_resolver.cpp](../src/detection/corners/measurement/corner_resolver.cpp)、`corner_observation.*` | 只接物理序四边形；统一以前邻→当前、当前→后邻给边提示。原图每帧分割一次；四角不能复用同一原图组件。删回调Source、工作图组件双向映射缓存与模型投影 |
| 两条拟合路线 | [corner_edge_fit.cpp](../src/detection/corners/fitting/corner_edge_fit.cpp)、`periodic_line_fit.*` | 普通L2主路，失败后周期相位中位数＋后验位置回退。删零斜率、独立周期OLS、L1及全端点退路；拟合缓存从6槽减2槽，支持描述只留一个域 |
| 改候选排序 | 同上 | `s=(r0+r1)√(1/n0+1/n1)`，减轻短支持弧偶然低残差的偏好；无新增门、预算或可调系数。error仍为r0+r1，不将s伪装成误差或协方差 |
| 精简CornerEvidence | [corner_types.hpp](../include/vision/types/detection/corner_types.hpp)、`corner_evidence_validation.cpp` | 删除stable_id字符串和模型边／顶点ID；保留物理身份、当前帧／原图组件、连续弧索引与像素、拟合线、double交点、raw／校正残差、extension、corner_error和质量输出 |
| 重写编排 | [decode_stage.cpp](../src/detection/pipeline/decode/decode_stage.cpp)、`detector.cpp` | Detector不再加载部件模型；按独立匹配调用拟合／排序／校验／发布。删不再需要的方向假设语义归并及其测试，M身份由match契约保证 |
| 配置、测试、构建清理 | `config/detector*.yaml`、`src/config/detector`、检测CMake及测试 | 删assignment_completion全部键、父验证／面积比／搜索上限、检测marker_geometry_path、原图组件映射预算及allowlist。原64px²内部边对分歧预算改名max_pair_disagreement_px2，数值不变；旧键严格报错 |

这里的“外框”始终指80×80白色MARK的结构外框。115×120外板、屏幕boundingRect或三个L推测的第四角不能作输入。22/80是30/80与14/80的结构中点；它不具有严格透视不变性。只有通过结构匹配的粗框才进入拟合，精角始终从原图真实两边求交，不能用粗框顶点直接发布。

支持提取、拟合、求交、证据、屏幕排序、验证、发布仍是明确步骤；没有新增接口类、策略层、工厂或运行模式。G2图纸取点表仍保存固定part/vertex，删除其中不再使用的部件边号。G2的IPPE、多平面解、原重投影screening、G4的NIS、coast与冻结输出含义保持。

## 4. 为什么选这个方案

借鉴[AprilTag 2原论文](https://april.eecs.umich.edu/media/media/media/pdfs/wang2016iros.pdf)的粗四边形与边界精定位分工，但MARK是离散L/M白片，不移植完整连续边框、编码规则或其拒绝阈值。[OpenCV拟合文档](https://docs.opencv.org/4.13.0/d3/dc0/group__imgproc__shape.html)用于区分L2与稳健估计；实现继续使用现有OpenCV4.6接口。

| 方案 | 实测／适用性 | 取舍 |
|---|---|---|
| 只留普通L2 | 新四边形录像可全部恢复；冻结旧frame784输入仍需回退 | 不选单路，避免因一种提示几何／采样结构失败而丢掉已知可恢复角 |
| 两路、保留原残差排序 | 1676帧无漏检且PnP分布略好；合成共同集角点中位／P95约上升0.004px | 没有直接交付这个初版 |
| **两路＋支持长度排序** | 共同合成真值中位不变、尾部降低；实拍PnP中位／P95／最大均降低，覆盖不降 | **本轮采用**；不增加拒绝门或阈值 |
| 加量化噪声底和外推杠杆的评分 | 做了72视角对照；共同集均值0.435828px，简单方案0.435260px，其余主要分位相同 | 公式更复杂，未显示收益，不采用 |
| Huber／RANSAC／换模型 | 本轮未实现；它们对离群点可能有用，但当前已知难例是扫描相位与边提示，短M支持有限 | 没有证据证明更准，不增稳健尺度、采样循环或模型选择机制 |
| 灰度梯度／亚像素加权边界定位 | 可作出现新ISP／失焦问题时的后续候选；当前没有该硬件影像对照 | 需要曝光、梯度和支持权重验证，当前不实施 |
| 全局受约束四边形／学习模型 | 不能把世界正方形直接约束为图像直角／平行；学习方法还需比赛域数据和板端资源验证 | 本轮证据不足，不增加整套变换或学习链 |

周期回退只处理明确的奇偶扫描结构。主路有合法结果时不主动启用它，避免假定更换相机后仍有同样周期噪声。支持长度分数不依赖帧号、相机型号、MARK屏幕位置或PnP结果；相邻轮廓点相关，所以它只是排序正则化，不是统计精度证明。未套用BIC进行模型自动选择，相关采样和中位数估计不满足直接套高斯极大似然的前提。

## 5. 1676帧完整对照

输入：`data/raw/marker_video.avi`，1440×1080，70.408336fps，SHA256=`aa1219a7a7b702ea1265be8752853a267c982a651afa0f7846f516f9517c9ac7`。固定K/dist、G2预算31.2166／33.4116px及G4配置，没有为验收改阈值或选最小候选发布。

| 指标 | b600635 | 最终实现 |
|---|---:|---:|
| 输入帧 | 1676 | 1676 |
| 检出／未检出帧 | 1088／588 | 1088／588 |
| 丢失／新增检出帧 | — | 0／0 |
| 候选角拟合调用 | 4360 | 4352 |
| 拟合失败 | 0 | 0 |
| PnP状态 | Ambiguous 1088 | Ambiguous 1088 |
| 最佳正深度候选RMSE中位 | 0.3915354321px | **0.3824287630px** |
| P95（nearest rank） | 0.7973333225px | **0.7663300749px** |
| 最大 | 1.1974446734px | **1.0370588351px** |
| 均值 | 0.4124007672px | 0.3999580809px |

**4360没有漏跑。**先在旧输入上独立消融：只并路线时，4360次全部成功，4359主路＋1回退，除路线编号外所有数值逐项完全相同；将原轮廓与边提示冻结后，再由当前构建的拟合器直接重放全部4360旧输入，仍4359主路＋1回退、0失败。新match不再产生重复父解释，frame313、382各少一个相同目标的父候选，故最终是1088×4=4352次。精度比较按同一1088帧、同一四物理角，未通过漏检难帧美化指标。

新实现的4352次全走L2。frame784 P1由旧 `(352.3113008891,322)` 改为 `(352.5653669311,322)`，位移0.254066px；整帧RMSE从0.812881降至0.712787px。用户已接受新边提示下由主路恢复。冻结的[frame784原图轮廓与旧边提示](../tests/detection/data/frame784-p1.txt)仍在自动测试中检查**必须走第二路**并恢复原交点（1e-8px容差）；没有按帧号强行回退。

4352角的新旧位移：中位0，P95=0.587441px，最大1.907949px；2084角变化超过1e-8px。位移是版本差异，不是真值误差。逐帧PnP有503帧变小、349帧变大、236帧在1e-9内相同；最大单帧增加0.497713px。因此不能宣称每帧更好。本轮“不退化”按相同帧集合的中位／P95／最大值、独立真值角误差和覆盖率一起判断。

### 残差与质量没有隐藏

下面使用共同8704条边，raw残差始终为原像素到物理直线的垂距。

| 指标 | 之前 | 之后 |
|---|---:|---:|
| raw平均残差的全边均值 | 0.142563px | 0.145431px |
| raw平均残差的P95 | 0.499479px | 0.499953px |
| raw平均残差的最大 | 0.712137px | 0.712137px |
| extension P95／最大 | 8.455798／17.754948px | 8.361077／17.754948px |

支持较长的弧可包含更多真实偏离像素，raw均值略升；不能用最小样本残差冒充更准确的交点。独立角真值与PnP结果支持本轮取舍，原始数值及超限标签均保留。检出帧的质量旗0/1/4/5/7，分别从794/132/126/35/1变为673/241/101/72/1，说明标签没有被屏蔽或靠丢测量降低。corner_error仍是交点到实测转折弧的距离，不是独立角点真值误差。

历史“好解中位约0.43px”与本轮“所有检出帧的最佳正深度候选”样本／筛选口径不同。本轮以同一视频、同一预算的0.391535基线比较；统计最小值不回馈主链。1088帧仍全部Ambiguous，没有挑一个解改成Unique。

### 全链输出和G4边界

`marker_app`完整输出observations.yaml、tracks.yaml、overlay.mp4、summary.yaml；视频逐帧解码为1676帧1440×1080。FFmpeg仍对原MJPG+MP4配置回退为mp4v，视频可完整解码，本轮没有改编码参数。

同配置重跑旧版后，G4 tracks.yaml与新版**逐字节相同**，SHA256=`5cf4ebc5595d40c2ab19bdb69baf4646cd6b46b22403535d846eadff0661e19c`。生产tracking.yaml的profile仍为空，所以1676帧均TRACKING_PROFILE_NOT_READY；同时本录像没有Unique位姿。不能把空track称为真实目标跟踪成功。

`offline_chain_test`另用独立已知透视24帧和显式测试预算／profile，验证22帧Unique→Measured、2帧缺失→Coasting，输出位置及表观速度；未知时间和qualified_for_g5保持false。生产G2预算／G4 profile未被测试值回填。NIS、coast与事务提交另有原跟踪契约测试。真实Unique与实测profile上的G4验收仍须实际数据。

## 6. 独立真值与其他现有材料

用图纸多边形和K/dist生成72个已知3D姿态的MJPG视角：深度0.45/0.65/0.9m，倾斜0.05/0.25/0.5rad，8种平面转角。扫描自然产生10个实际出框视角，新版均未检出；剩余62个完整视角全部得到四角。渲染真值不提供给检测或拟合。

| 真值指标 | 旧版 | 新版 |
|---|---:|---:|
| 完整四角视角 | 43 | 62 |
| 旧版有、新版无 | — | 0 |
| 共同172角：中位误差 | 0.427636562px | **0.427636562px** |
| 共同172角：P95 | 0.789776474px | **0.745009910px** |
| 共同172角：最大 | 0.963514747px | **0.922374081px** |
| 共同172角：均值 | 0.440838452px | 0.435260182px |
| 新版全部248角：中位／P95／最大 | — | 0.408703／0.742906／0.922374px |

这验证了栅格化、旋转、透视、缩放下的角定位与身份；不覆盖真实镜头／ISP／反光噪声。真实录像无独立角点标注，不能由其重投影残差证明绝对角精度。

另外完整复跑 `data/raw/calibration_video.avi`：6966帧1440×1080，圆点标定板、人体和背景，**0匹配四边形、0拟合失败**。它是现有非MARK负样本，不能代表比赛现场全部背景。

## 7. 构建与测试

| 构建 | 结果 |
|---|---|
| MARK_BUILD_G2=ON、MARK_BUILD_G4=ON | 构建通过，**17/17** CTest通过 |
| G2=OFF、G4=OFF | 构建通过，**13/13** 通过 |
| G2=ON、G4=OFF | 构建通过，**15/15** 通过 |
| BUILD_TESTING=OFF、VISION_BUILD_TOOLS=OFF，G2/G4开启 | 库构建通过 |

原23个测试删除了7个已删功能的入口（对应补全、锚点／三L搜索、父验证、旧竞争链、语义归并、手工父验证），新增match_quad_test，最终17个。保留并适配角拟合、数值／浮点发布、严格配置、物理模型、IPPE、多解筛选、跟踪及完整离线链测试。

新增／适配断言包括：六种旋转×三种工作尺寸（含各向异性）、物理身份和独立真角；缺M、矩形替M、转错L、孤立亮矩形、缺一个S、分离双MARK；黑原图、帧来源、映射非法、出框、短支持；周期满秩／相位参数、frame784冻结回退；质量阈值降到0仍保持测量；独立参考枚举器核对全部合格弧／配对及claimed证据复算。测试参考也删掉不用的路线并改名edge_fit_reference。

最终API清理后再次跑1676帧角拟合，与完整主链所用角结果的TSV SHA256一致：`b7e143d7430dd6d03bb36f51776a35e332ba938ec92ca41103338f05f5a0bf6d`。本机完整视频约46.5s，期间并行构建／对照；这不是板端吞吐基准，不据此承诺MaixCam帧率。

## 8. MaixCam与比赛现场待验清单

设计上使用图纸比例、四边形邻边和当前原图支持，不按录像帧号、固定屏幕位置、仿射平行边或相机专属斜率拟合。[Sipeed相机说明](https://wiki.sipeed.com/hardware/en/maixcam/cameras.html)列有不同快门／镜头传感器，[相机使用文档](https://github.com/sipeed/MaixPy/blob/main/docs/doc/en/vision/camera.md)说明默认手动调焦及曝光／增益控制；比赛实物传感器和镜头尚未确认，不假定本录像的奇偶效应可照搬。

| 待验项 | 当前设计／必须量测 |
|---|---|
| K/dist及像素域 | 用实际镜头、采集分辨率、裁剪／resize状态标定；G2与检测点必须同域。明显畸变造成曲边时，需一致矫正域后重验，不能只替换PnP内参 |
| 像素信息量 | 当前min_line_points=10、min_support_span=14px、trim=2px。粗估短M原边需≥18px可用长度，即近正视MARK边长约≥103px；极端透视／圆角／模糊需更多。低分辨率远目标不能靠降门宣称精度保持，优先保证采样分辨率／距离 |
| 定位与质量预算 | 原图direction=3°、position=9px、connection=13.5px、extension标签=9.5px、角误差标签=4.131595px、残差标签=0.5px；原图epsilon=1、工作epsilon=2、边对分歧64px²。按fx比例给像素预算迁移初值，平方预算按比例平方；点数／角度不能盲目同比例缩放。逐项复验覆盖率、误差、标签分布 |
| M身份与透视 | 22/80结构分界、六边拓扑、凸包近似在远近／大倾角／边缘视场／实体圆角上复验；它们不是严格投影不变量。近邻目标或强L形背景可能混组，需负样本及重捕验证 |
| 曝光／ISP／照明 | 手动对焦、曝光、增益、白平衡、锐化、降噪、灯光频闪、过曝连接、反光断片、运动模糊；固定白阈值200只是现配置。用实际像素速度确定曝光，规则0.5m/s不能代替相机相对运动量测 |
| 回退与其他失败 | 检查奇偶／其他周期残差、两相位样本和满秩；无相同噪声时主路照常。验证缺M、局部出框、遮挡、相邻MARK、目标消失与重捕；没有证据就交G4 coast，不能补成检测成功 |
| G2／G4／时间 | 保留IPPE与原screening，核对多解状态；实测profile、相机系表观速度、NIS和coast，未知曝光时间仍未知，G3/G5资格不凭接收时间补真值 |
| 计算预算 | 全原图分割与连续弧搜索要测板端耗时／峰值内存／热稳定。局部分组最坏O(N²)，强碎噪声下当前没有新资源截断门；需实拍测工作分辨率、面积下限与噪声数量，不能把桌面速度当保证 |

上述是待验项，不是已经完成的硬件验收。当前实现没有为了设备迁移新增质量拒绝门、依赖或运行模式。

## 9. 可复算材料与命令

[validation/match-only](validation/match-only)保存原4360、两路线消融、最终4352、合成前后TSV、前后PnP输出、最终G4输出和真值，均为gzip；[metrics.json](validation/match-only/metrics.json)为机器计算统计，[manifest.json](validation/match-only/manifest.json)记录压缩前后SHA256。完整视频输出保留在 `/tmp/match-final-replay/`；同配置旧版在 `/tmp/match-controlled-before-replay/`。

```bash
cmake -S src/tushenghao -B build -DMARK_BUILD_G2=ON -DMARK_BUILD_G4=ON
cmake --build build -j 4
ctest --test-dir build --output-on-failure
# 默认output目录须不存在；另一次运行先在配置副本设置output.run_directory。
build/marker_app --video data/raw/marker_video.avi --config src/tushenghao/config/detector.yaml
build/corner_route_test data/raw/marker_video.avi /tmp/routes.tsv
build/corner_route_test data/raw/calibration_video.avi /tmp/negative.tsv
gzip -dc src/tushenghao/docs/validation/match-only/frozen-inputs-4360.txt.gz > /tmp/frozen-inputs.txt
build/corner_route_test --frozen /tmp/frozen-inputs.txt /tmp/current-frozen.tsv
python3 src/tushenghao/tests/detection/replay_metrics.py generate /tmp/new-truth
# 生成truth.avi和truth.json；目录必须不存在，生成依赖Python OpenCV+NumPy，非生产依赖。
```

直接重算已归档结果：

```bash
python3 src/tushenghao/tests/detection/replay_metrics.py compare \
  --before-fits src/tushenghao/docs/validation/match-only/baseline-4360.tsv.gz \
  --after-fits src/tushenghao/docs/validation/match-only/final-4352.tsv.gz \
  --before-poses src/tushenghao/docs/validation/match-only/observations-before.yaml.gz \
  --after-poses src/tushenghao/docs/validation/match-only/observations-after.yaml.gz \
  --truth src/tushenghao/docs/validation/match-only/synthetic-truth.json \
  --before-truth-fits src/tushenghao/docs/validation/match-only/synthetic-before.tsv.gz \
  --after-truth-fits src/tushenghao/docs/validation/match-only/synthetic-after.tsv.gz \
  --output /tmp/metrics.json
```

基线可从 `git archive b600635 src/tushenghao` 导出到临时目录单独构建marker_app、corner_route_test；为其使用旧schema的detector配置，G2/G4资源与当前对照保持相同。不要把旧配置里的已删键写回新版。脚本仅分析输出，不参与生产筛选。
