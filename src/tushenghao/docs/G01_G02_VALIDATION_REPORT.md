# G-01真实Unique、G-02条件NIS与视频兼容性验证

> 历史快照：后续单候选拟合与156项裁决已实施，现状见[最新报告](SINGLE_CANDIDATE_SLIMMING_REPORT.md)和[flow-guide](flow-guide.md)。本文保留当时验证和接口含义，不覆盖当前代码。

基线：`refactor/vision-lean`的`a582c48`。本轮不改检测、角拟合和G4实现／阈值；G2保留IPPE、两个候选、原正深度和重投影screening。生产G4 profile仍为空；经用户确认，用已有合成profile的原数值作临时回放条件诊断，不冒充这段录像的实测标定。

## 先量数：a582c48完整1676帧

重新运行原marker_app，输入`data/raw/marker_video.avi`，1676帧1440×1080，检出1088、未检出588、1088全Ambiguous、NoSolution=0。新的基线observations.yaml SHA256=`d9d414a9a2d033939b5dd7bf9f8f9cbf345df8267942544ebd59fddce0eeed89`，与a582c48归档完全一致。

| 指标（1088个双候选） | 中位 | P95 | 最大 |
|---|---:|---:|---:|
| 好支RMSE | 0.382428763 | 0.766330075 | 1.037058835 |
| 差支RMSE | 1.391270584 | 2.040822723 | 2.521167028 |
| 差距bad−good | 1.012715095 | 1.772841826 | 2.354763436 |
| 比值bad/good | 3.666939371 | 16.083805113 | 114.507078068 |
| 好支最大单角重投影误差 | 0.386287134 | 0.772925870 | 1.059036208 |

RMSE／差距单位为原图px，比值无量纲。nearest-rank P95。最小差距仅0.000149664px，比值1.000165051，确有不可直接消歧的帧。

IPPE按误差排序，原始槽位argmin恒为0，所以槽位翻转率0不是物理分支稳定证据。对1081对相邻检出帧，用两种一对一排列中SO(3)旋转测地距离和最小者配对两支，最佳分支切换14次（1.2951%）；不跨未检出间隔配对。它是连续性诊断，无独立姿态真值，不能称错误分支率。原始候选见压缩YAML，逐相邻帧统计见[baseline-candidates.json](validation/g01/baseline-candidates.json)。

## 阈值决定（先冻结，再实施）

| 参数 | a582c48 | 本轮 |
|---|---:|---:|
| max_rmse_px | 31.2166 | 1.10 |
| max_corner_error_px | 33.4116 | 1.10 |
| selection.min_rmse_gap_px | 无 | 0.25 |

新增差距字段可不配置或留空，保持旧调用方语义；配置时必须为有限正值。生产配置显式启用，未添加新的整帧拒绝状态。

- `max_rmse_px=1.10`，`max_corner_error_px=1.10`：分别覆盖本片所有好支的1.037059／1.059036px最大值，按0.1px粒度留余量，不贴着某一帧设精确边界。保持1088个最小误差支全部通过绝对预算，NoSolution不增加。
- `selection.min_rmse_gap_px=0.25`：唯一新增的候选消歧参数，不是整帧拒绝门。普通数值／正深度及原有绝对预算先执行；只要仍有候选且存在两个数值合法、正深度且未被原安装约束／可信prior排除的IPPE解，就要求两支RMSE相差至少0.25px才能发布Unique。另一支即便刚越绝对预算，也不能绕过差距保护；不足则Ambiguous，绝不改成NoSolution。没有第二个物理合法解时沿用原筛选结果。
- 充分分离且仍双合格时，保留实际较小RMSE支，将另一候选标记为消歧未选中；两支原始R/t、误差、深度与原因都保留，供G4检查恰一个eligible。没有把“槽位0”或“较小一点”直接当Unique。

选择依据：全体原始合法候选差距0.10／0.20／0.25／0.30px分别预计1078／1065／1054／1044帧Unique；相邻双方均Unique时的旋转配对分支切换分别10／2／0／0次。0.25是有意义的四分之一像素粒度，进入观察到的稳定区，较0.30多保留10帧；没有取0.2382等恰好越过个别帧的精细数值。不是统计置信概率或对新相机的保证。最终语义的完整敏感性结果见[results.json的threshold_sensitivity](validation/g01/results.json)；首次探索日志保留未采用方案，二者不混用。

比值1.5联合0.25预计1029帧Unique，比值2联合0.25仅913；本片未见额外连续性收益，所以不再增加比值参数。IPPE作者建议用充分的重投影分离识别可区分解，同时指出近仿射成像的二义性不能靠强选消除，见[原作者说明](https://github.com/tobycollins/IPPE)。本轮使用明确像素差距，不把未经标定的角点噪声代入似然概率。

同一录像用于量数与阈值选择；分段敏感性不是独立采集holdout。换MaixCam需按实际像素域／fx迁移并重验，尤其不能把本片的零连续Unique分支切换当通用正确率。

## 实施与完整复跑

源代码提交：G2 `6a79929`，视频 `3ddfce4`。完整复跑两次：默认生产配置（仅换输出目录）、临时条件G4配置。两次observations逐字节一致，SHA256=`d997f00ed0c699dac629aefb899e368e383663a1cd7a7dc080b5968e013275af`。统计可用[分析脚本与原始数据](validation/g01/README.md)复算。

| 1676帧结果 | a582c48 | 本轮 |
|---|---:|---:|
| 检出／未检出 | 1088／588 | 1088／588 |
| Unique | 0 | **1054** |
| Ambiguous | 1088 | **34** |
| NoSolution | 0 | **0** |
| InvalidInput／CorrespondenceMissing／BudgetMissing | 0 | 0 |

Unique覆盖检出帧96.875%。34个小差距帧仍不发布位姿；frame 228、555、1455虽只剩一个绝对预算合格支，但另一物理合法支与其相差不足0.25px，仍Ambiguous。没有靠绝对预算边界硬造Unique。

| RMSE，原图px | 样本数 | 中位 | P95 | 最大 |
|---|---:|---:|---:|---:|
| 基线全部检出帧最佳正深度候选 | 1088 | 0.382428763 | 0.766330075 | 1.037058835 |
| 新代码同一1088帧、同一统计口径 | 1088 | **0.382428763** | **0.766330075** | **1.037058835** |
| 本轮实际发布Unique | 1054 | 0.371927658 | 0.734203034 | 0.960701087 |

对2176个候选逐值核对R/t、RMSE、最大单角误差、最小深度、数值有效旗，及1088帧角点质量旗，**完全不变**。只变筛选状态、eligible及消歧原因。Unique子集较低的分布是筛选效应，不是拟合精度提升；相同1088帧口径完全相同才是本轮“不退化”的证据。检测和角拟合代码／参数均未动，真实录像仍没有独立角点／姿态真值，不能从较低RMSE推导绝对姿态正确率。

连续且双方均Unique的1016对中，按上述SO(3)配对的最优分支切换0次；基线14次切换的邻接条件被小差距Ambiguous打断。它说明没有在已发布的相邻Unique间观察到此类跳支，不代表跨歧义／失检间隔也没有跳支。

### 实际Unique重验原G4

生产`tracking.yaml`的profile仍为空，原样运行得到1676帧NOT_READY。用户明确无本片专用profile／独立真值，并授权复用现有profile作条件诊断。临时配置只把profile指向仓库既有`evidence/g4/camera_relative-nominal_research-calibration/profile.yaml`的逐字节副本；没有改其中数值、camera_id或证据身份，更没有写回生产配置。

| 条件回放输出 | 数量 |
|---|---:|
| 实际G2 Unique输入 | 1054 |
| G4 MEASURED | **1050** |
| COASTING | 53 |
| LOST（每tick冻结状态计数） | 573 |
| 发布表观速度 | 1011 |
| 创建新轨迹／reseed新ID／pending-reseed | 13／3／4 |
| 位置不确定度超限Lost事件 | 9 |
| 门前创新被原NIS拒绝 | 7 |

MEASURED+COASTING+LOST=1676。1054次Unique中4次未作为当帧实测更新；7次门前拒绝包括随后由原reseed逻辑接管的情况，因此不能用7直接相减。原NIS、bootstrap、coast、Lost、质量标签和适配器均保持。离线探针重放得到的tracks与实际marker_app输出逐字节一致，SHA256=`ff624a80848a71679fd2a944d0a189683663c67ebbd50cd57e62deaa8695c500`。

**G-01本次要求的真实图像→Unique→原G4条件数值链已闭环；硬件适用域未获验证。** profile仍标`synthetic-camera-g4`，与本片`reference-maxicam-calibration`不同，1676帧均如实保留`PROFILE_PROCESS_FRAME_MISMATCH`。离线名义时轴由源fps生成，现有runner的InputOrigin为Synthetic，含义是构造的回放时轴；图像本身是真实录像。曝光与时间误差界未知，1676帧均保留时间域警告，time_bounded／qualified均false。本结果不补造时间证据，不授予MaixCam／G5控制资格。

## G-02：门前NIS条件一致性检查（只量数）

定义NIS=`rᵀ S⁻¹ r`，`S=H P⁻ Hᵀ+R_eff`，三维位置创新。若只统计已经过NIS门的更新，会把大尾部截掉；所以独立只读探针调用**同一未修改CvFilter数学**，跟随真实TargetTracker的创建／校正／重建决定，采集门前NIS。它不改门、不强制更新。逐帧核对1103个live状态的位置和完整P、1011个公开速度，以及1037个已接受NIS，最大差均为0，详见[探针校验](validation/g01/nis-probe-check.txt)。

使用profile原数值：`q=0.008264593245685344 m²/s³`，初始速度方差`0.1309447803068589 m²/s²`，关联NIS门`12.564998215210759`，bootstrap门`6.150637816384821`，max_dt=`0.01762631789996s`。R完整矩阵及其SHA见归档[reference-profile.yaml](validation/g01/reference-profile.yaml)和[manifest.json](validation/g01/manifest.json)。未知时间下生产实现采用含时间二阶矩的保守MSE增广；这不是已验证的高斯噪声标定。

| 采样组 | n | NIS均值 | 中位 | P95 | 最大 |
|---|---:|---:|---:|---:|---:|
| 全部门前（含未确认seed） | 1044 | 1.496559 | 0.395637 | 5.854539 | 42.992907 |
| **已确认轨迹门前（主诊断）** | **1035** | **1.501627** | **0.392615** | **5.937977** | **42.992907** |
| 未确认seed门前 | 9 | 0.913765 | 0.484774 | 4.633957 | 4.633957 |
| 仅已接受（有截尾偏差） | 1037 | 1.342259 | 0.389480 | 5.719148 | 12.547338 |
| 拒绝尾部 | 7 | 24.355086 | 22.121339 | 42.992907 | 42.992907 |

拒绝帧为207、229、230、662、663、683、684（0起始）。假设零均值独立高斯且S就是正确创新协方差，χ²(3)均值为3、单边95%点7.814728、双边95%区间[0.215795,9.348404]。实际已确认门前样本超过单边95%点20/1035=1.9324%；落入双边95%区间587/1035=56.7150%，低于下界440个、高于上界8个。均值偏低且有少量长尾，不符合简单统一高斯缩放。

进一步以S的Cholesky因子白化已确认门前残差：均值[-0.115500,-0.001743,-0.004453]，二阶矩特征值[0.007305,0.246492,1.247830]；同ID、无reseed的连续994对，一阶相关[0.933349,0.697030,0.647517]。创新明显有方向差异和时间相关，不以独立样本公式给ANIS置信区间。NIS可在无真实状态时检验创新假设，但不是速度真值误差，参见[Piché的原论文](https://onlinelibrary.wiley.com/doi/10.1002/acs.2571)。

**速度协方差低估倍数：无法由现有数据唯一确定，留空而不编倍数。** 若仅做“统一缩放整个S，使平均NIS等于3”的描述性计算，已确认样本比例为1.501627/3=**0.500542**，标准差比例0.707490；这是整个创新S的均值匹配量，不是Pvv比例，也不是建议调参。过大的R可以同时掩盖低估的速度Pvv；未知时间的MSE包络、相机profile错配和强相关进一步限制解释。不能把7个拒绝尾部的均值/3=8.118当成速度低估倍数。

当前模型报告的速度std（1103个live状态，最大特征方向）中位0.030821m/s，P95=0.125575m/s，最大0.558800m/s；这是模型输出，不是经验误差。本轮未改滤波器、R/Q、NIS或其他阈值。G-02量数交付完成，但真实速度误差／协方差校准仍OPEN，需要同步独立位置／速度真值及MaixCam实际时间信息。

## 视频编码与完整性

改为**AVI容器＋MJPG编码**，`overlay.partial.avi`写完、关闭、逐帧读回后发布`overlay.avi`。生产四字符仍为MJPG，修正的是MP4不支持该组合而回落mp4v的容器错配；不改画面渲染、检测／位姿／跟踪输入或时间戳。Windows Media Foundation原生列出AVI源和MJPEG解码器，见[Microsoft支持格式文档](https://learn.microsoft.com/en-us/windows/win32/medfound/supported-media-formats-in-media-foundation)。选择它可避免依赖本机OpenCV是否编入H.264编码器，代价是文件较大。

两次最终完整复跑均已通过exporter逐帧读回；独立ffprobe `-count_frames`也均为：`format=avi`、`codec=mjpeg`、`tag=MJPG`、1440×1080、**1676帧**。AVI读回帧率8801/125=70.408，与源约70.408336fps的整数微秒帧周期相同，既有校验通过。记录见[默认编码读回](validation/g01/ffprobe-final.json)和[条件编码读回](validation/g01/ffprobe-conditional.json)。本环境完成解码验证，未声称在Windows GUI上实测。

本地最终视频：`/tmp/g01-final-replay/overlay.avi`（默认profile）与`/tmp/g01-conditional-replay/overlay.avi`（条件G4可视化）。大体积视频不入Git，输入、输出hash与复跑命令已归档。

## 改动位置、构建与回归

| 改动 | 文件／验证 |
|---|---|
| G2绝对预算和可选充分差距 | `config/pose.yaml`、`include/vision/config/pose_config.hpp`、`src/config/pose/pose_config.cpp`、`src/pose/estimation/pose_estimator.cpp`；不改求解／精修／检测／拟合／G4 |
| G2语义测试 | `tests/pose/estimation/pose_contract_test.cpp`：充分分离Unique、小差距Ambiguous、绝对预算跨界仍小差距、全坏NoSolution、保留独立prior结果、参数非法值及原始候选逐值不变；`tests/support/pose/g2_test_scene.hpp`显式隔离旧测试预算与本轮生产调参 |
| AVI导出和默认文件名 | `src/app/export/video_exporter.cpp`、`src/config/application/app_config.hpp`、`src/config/detector/config.cpp`、三份detector YAML；offline_chain_test检查实际MJPG四字符、全帧读回及原22次Unique实测／2次coast |
| 文档统一 | 仓库根原`docs/`、先前根目录报告均整理到`src/tushenghao/docs/`；修复README／ASSIGNMENT／data依赖链接与当前入口，历史证据快照保持当时语义；flow-guide原位更新G2，视频步骤只同步本次文件名事实 |
| 条件诊断材料 | `docs/validation/g01/`为离线分析和证据，不接入生产CMake，不增加模块抽象或运行门控 |

Release全链G2+G4构建成功、CTest **17/17**；仅检测 **13/13**；G2不含G4 **15/15**；`VISION_BUILD_TOOLS=OFF, BUILD_TESTING=OFF`库构建成功。日志全部归档于[validation/g01](validation/g01/README.md)。检测／角拟合、tracking源码、生产tracking.yaml、calibration.yaml相对a582c48无差异；main、refactor/lean不改。

## 比赛场景与后续验证边界

模型仍为MARK白片结构80×80mm、三L＋一短M固定身份，识别板115×120mm，沿用[MATCH_ONLY_RULES](MATCH_ONLY_RULES.md)对V1.0.3的核对；本轮没有改变对应或拿外板代替模型。G2消歧依据两个真实IPPE解在同一K/dist与RAW像素上的误差，角残差／extension／角误差仍只标质量。没有利用帧号、人工姿态或G4预测来制造Unique。

MaixCam需重新测K/dist、实际像素域／尺寸、曝光与时钟界，覆盖比赛灯光、白片反光／低对比、近正视平面歧义、远小目标、斜视与运动模糊、遮挡及兑换区相机自身运动。复验绝对预算和0.25px差距的覆盖／假消歧，保留小差距Ambiguous；固定设备后再用独立会话冻结与验证阈值。本轮像素数值仅保证这段录像，不是跨相机精度／鲁棒性证明。

提高比赛成功率的优先项是现场成像与标定验证、保持L/M完整身份和独立时间证据；近仿射二义性无法靠继续缩阈值消除。若以后有独立可信安装域或prior，可用已有接口验证；当前不新增时序硬选、平滑补点、拟合路线或拒绝门。详见[复现步骤与hash](validation/g01/README.md)。
