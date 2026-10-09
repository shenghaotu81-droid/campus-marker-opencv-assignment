# 112 恢复验收

状态：完成，全部实现与实测验收通过，证据已封存，本轮专用build已清理。分支 `fix/112-recovery`，起点 `4ad0a349cf5ee5bf13c2c35256e8beb4d3810d7c` 与 main 相同。
执行依据：[实现提示词](ref/codex-112-implementation-prompt.md)、[逐文件方案](evidence/112-sandbox/EXECUTION_PLAN.md)、[快速路线](evidence/112-worstframe/REPORT.md)。阅读代码见 [施工记录](fix112_recovery_log.md)。

## R01–R09 实测

| ID | 结果 | 标准与实测证据 |
|---|---|---|
| R01 | PASS | 完整 frame0..1675 共1676帧，1088检测，指定112/112恢复；原976帧status/raw四角逐值不变。两次最终raw相同；[第一次](evidence/112-recovery/final-repeat1/summary.json)、[第二次](evidence/112-recovery/final-repeat2/summary.json)。 |
| R02 | PASS | 旧损失0、集合外新增0、集合外status变化0；新增集合严格等于冻结112 ID；[独立对账](evidence/112-recovery/summary.json)。 |
| R03 | PASS | Release、OpenCV单线程、顺序运行、公共process含Summary；两次mean≤25ms、max≤33ms、超33ms均0；完整分布见下表。 |
| R04 | PASS | Release与Debug分别25/25；Debug实际编译 flags 为 `-g -std=gnu++17`，没有NDEBUG，断言启用；[Release日志](evidence/112-recovery/final-release-ctest.log)、[Debug日志](evidence/112-recovery/final-debug-ctest.log)、[真实构建参数](evidence/112-recovery/build-metadata/)。 |
| R05 | PASS | 固定20/20负样本全部拒绝：全黑原图与15px半径缺角，含0/1px扰动；[88条独立测试原始结果](evidence/112-recovery/final-test.jsonl)。 |
| R06 | PASS | 固定种子7919、68个正样本，67/68通过，最大真值误差1.401158px<2px。固定ID2的2px压力案例拒绝。样本ID/角度/幅度/局部模式/负样本类型逐项与原封存输入相同；[汇总](evidence/112-recovery/summary.json)。 |
| R07 | PASS | 每次完整视频JSONL、日志、manifest及该步生产源码快照均归档；保留性能未达标探索，不删慢帧；[证据索引](evidence/112-recovery/INDEX.md)、[封存清单](evidence/112-recovery/manifest.json)。 |
| R08 | PASS | 实现记录与本验收已写完；各逻辑改动有中文因果注释。17个修改/新增C++文件按自有.clang-format检查，无差异；[格式日志](evidence/112-recovery/clang-format-check.log)。 |
| R09 | PASS | evidence/INDEX与docs/INDEX增加入口；README原字节完整归档，更新路径与限制段落。公共头、Detection/FrameResult布局、YAML、ref均不变；[保护检查](evidence/112-recovery/summary.json)、[原README](history/README_before_112_recovery.md)。 |

## 完整性能分布

所有行均为完整1676帧，没有选取子集。分位数采用最近秩 `ceil(p*N)-1`；单位ms。

| 版本 | 检测 | 均值 | P50 | P95 | P99 | 最大 | 超33ms帧数 |
|---|---:|---:|---:|---:|---:|---:|---:|
| 原main基线 | 976 | 78.899 | 81.113 | 199.549 | 262.748 | 345.681 | 1060 |
| 仅帧内索引 | 976 | 78.334 | 78.850 | 203.355 | 264.179 | 355.224 | 1054 |
| 精确等价搜索优化 | 976 | 21.640 | 23.290 | 51.305 | 63.680 | 94.849 | 493 |
| A/B补查 | 996 | 30.635 | 24.658 | 58.385 | 346.315 | 463.920 | 546 |
| C1完整旧顺序 | 1088 | 34.750 | 24.686 | 66.808 | 396.439 | 2685.656 | 572 |
| 调用内缓存+快速路线初版 | 1088 | 9.165 | 10.262 | 19.572 | 25.624 | 39.866 | 3 |
| 描述scratch复用 | 1088 | 7.488 | 8.556 | 15.544 | 20.447 | 34.050 | 1 |
| 周期估计器scratch复用 | 1088 | 7.370 | 8.263 | 15.475 | 20.160 | 27.759 | 0 |
| 最终冻结复测1 | 1088 | 7.673 | 8.583 | 16.131 | 21.398 | 27.803 | 0 |
| 最终冻结复测2 | 1088 | 7.484 | 8.412 | 15.577 | 20.454 | 26.063 | 0 |

环境为Intel Core i9-14900HX、WSL/Linux、GCC13.3、OpenCV4.6、Release `-O3 -DNDEBUG`。`cv::setNumThreads(1)`，没有CPU绑核、增加工作线程、调高优先级或快数学。计时为steady_clock包围公共Detector::process，包含预处理、几何、decode、稳定、Summary及本轮缓存创建/释放；VideoCapture、JSON序列化和GUI在原计时范围外。最终两次运行与编译、回归、合成验证均错开，顺序完成。

上述早期性能未达标行完整保留，不能用“34.050约等于33”宣称通过。原基线从未修改main库构建；关闭补查的优化路径不冒充原性能基线。

## 原输出与快速路线变化

原976成功帧的raw四角逐值不变，两次最终运行raw逐值相同。与第四步完整旧顺序相比，快速路线改变21个已恢复帧的角点：150、177、193、300、332、366、369、376、383、409、422、480、511、565、678、695、784、984、1029、1043、1132；逐帧delta见 [summary.json](evidence/112-recovery/summary.json)。最大变化1.628526px，是两种解之间的差，不能当作相对实拍真值的误差。所有1088帧status/raw与历史快速路线输出也逐值相同。

完整枚举reference仅链接进测试。39组比较逐项覆盖全部合格弧、通过所有质量谓词的配对、枚举顺序、完整支持/连接弧、拟合参数、两类残差及最终tie-break，逐值全等；包括反向轮廓、回走、奇偶扰动、圆角、连接预算前后一ULP和不同修剪距离。[主动检查输出](evidence/112-recovery/final-reference.log)。

## 独立真值与证据契约

| 分区 | 种子 | 正样本 | 通过 | 最大真值误差px | 负样本误接受 | 实际周期参数篡改 |
|---|---:|---:|---:|---:|---:|---:|
| 校准 | 112 | 25 | 24 | 1.497220 | 0/0 | 11次，全部拒绝 |
| 测试 | 7919 | 68 | 67 | 1.401158 | 0/20 | 25次，全部拒绝 |

校准/测试分别保留全部0/1px正样本和固定ID2的2px压力样本；负样本在0/1px域内，避免高噪声先拒绝掩盖缺角判据。采样顺序、种子与冻结样本身份逐项一致；没有根据新性能或测试结果换样本、放宽0.5/9.0或修改真值。

单线与周期模型都由validator从完整有序原支持重建参数；raw垂距与校正垂距分别复算。原line_mean/max_residual和error的定义不变；周期模型0.5约束校正平均垂距，量纲来自整数栅格正交坐标量化上界，不声称新支持的raw残差也≤0.5。另有已知a/b/c、轴对称、单相位/秩退化、非有限输入与6类元数据篡改主动检查。

## 已知边界与归档

2px跨行位移形成宽链码，固定压力样本仍拒绝；这是保留的盲区。67/68是隔离角解析与生产validator的已知真值试验，工作图绑定作为测试已知输入，不代表上游三L生成的完整召回率。

1088/1676与112/112只保证这个固定视频集合。实拍没有独立亚像素标注，不报告实拍定位误差<2px或全场景零误检；原成功raw相等也不表示新增检测后的平滑历史不变。任意新输入仍保留完整fallback，未证明任意图像/硬件都满足33ms最坏界。

输入、配置、生产/测试源码、构建参数和所有归档文件的SHA256见 [manifest](evidence/112-recovery/manifest.json)。复现命令见 [REPRODUCE](evidence/112-recovery/REPRODUCE.md)。只清理本轮专用 `build/112-recovery/`；用户原有 `build/release` 保留。
