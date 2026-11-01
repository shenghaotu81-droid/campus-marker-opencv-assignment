# G4本地软件交付证据

分支feat/tracking，基线30ac7ab43e3c54576fbeba5264d66f5d61d6bad9。软件施工与离线流程完成，G-01/G-02/G-03均OPEN，未经过实车确认。执行依据仅[归档文档](../../ref/G4-Codex.md)与用户追加的bounded_only缺界审查要求。

## 文件与保护

- [修改23文件清单](modified-files.txt)、[新增20文件清单](new-files.txt)、[20文件SHA256](new-files.sha256)、[全部修改SHA256](modified-files.sha256)。执行依据G4-Codex.md是用户原先已暂存文件，本轮保持。
- [Step0基线](baseline.txt)、[中断恢复核对](resume-check.json)、[原5095文件hash](initial-tracked-sha256.json)、[冻结保护结果](freeze-check.json)、[G2空diff](frozen-g2.diff)。原5092文件完全不变，另3文件仅指定追加；[全原文件复核](frozen-original-hashes.log)退出0。
- [before.patch](before.patch)与[最终after-final.patch](after-final.patch)均包含用户预存的文档diff；[本轮原文件增量](incremental-tracked.patch)、[完整新增正文patch](new-files.patch)、[本轮完整delivery.patch](delivery.patch)区分初始状态与本轮修改。after.patch为README EOF修正前中间快照，最终以after-final.patch为准。
- [完整新源码及集成文件快照](source-snapshot/src/tushenghao/)、[相对文档的全部差异](document-deviations.patch)、[R2及审查备注说明/直接断言值](r2-and-review.md)。14文件仍逐字匹配第4节；4个代码文件仅审查修复/回归和安全解引用，2个文档更新实测。

## 命令与验证

[全部命令表](commands.md)、[逐条机器记录](commands.jsonl)、[工具链](toolchain.log)、[时间投入](timing.json)、[中断及辅助失败说明](workflow-notes.md)。

- [Release配置](configure-release.log) / [构建](build-release.log) / [CTest 29/29](ctest-release.log) / [主动断言输出](last-test-release.log)。
- [Debug配置](configure-debug.log) / [构建](build-debug.log) / [CTest 29/29](ctest-debug.log) / [主动断言输出](last-test-debug.log)。
- [ASan配置](configure-asan.log) / [构建](build-asan.log) / [沙箱首轮失败](ctest-asan.log) / [经授权沙箱外2/2](ctest-asan-unsandboxed.log) / [主动断言输出](last-test-asan-unsandboxed.log) / [说明](memory-check-note.md)。不把未插桩的第三方库当作本证据范围。
- [结果汇总JSON](results-final.json)、[统计/包含率/时间与旧输入核验](audit-results-final.log)、[本地验收文档](../../g4_acceptance.md)。

## 原始数据与四域证据

[14ms生成日志](generate-14ms.log)、[14ms manifest](data-14ms/manifest.yaml)、[33ms manifest](data-33ms/manifest.yaml)、[全部原始数据SHA256](raw-data.sha256)。两套数据各3 train/2 selection/3 holdout完整session，完整frames.yaml和truth.csv保留，不能只靠种子复现。

|域|标定与profile|holdout报告/完整CSV|单会话回放|速度样本|速度RMSE(m/s)|
|---|---|---|---|---:|---:|
|camera_relative-nominal_research|[calibration](camera_relative-nominal_research-calibration/calibration.md) / [profile](camera_relative-nominal_research-calibration/profile.yaml) / [研究配置](camera_relative-nominal_research-calibration/tracking_research.yaml)|[报告](camera_relative-nominal_research-holdout/verification.md) / [CSV](camera_relative-nominal_research-holdout/holdout_errors.csv)|[summary](camera_relative-nominal_research-replay/summary.yaml) / [tracks](camera_relative-nominal_research-replay/tracks.yaml)|12611|0.057740984|
|camera_relative-bounded_only|[calibration](camera_relative-bounded_only-calibration/calibration.md) / [profile](camera_relative-bounded_only-calibration/profile.yaml) / [研究配置](camera_relative-bounded_only-calibration/tracking_research.yaml)|[报告](camera_relative-bounded_only-holdout/verification.md) / [CSV](camera_relative-bounded_only-holdout/holdout_errors.csv)|[summary](camera_relative-bounded_only-replay/summary.yaml) / [tracks](camera_relative-bounded_only-replay/tracks.yaml)|12481|0.069801801|
|local_world-nominal_research|[calibration](local_world-nominal_research-calibration/calibration.md) / [profile](local_world-nominal_research-calibration/profile.yaml) / [研究配置](local_world-nominal_research-calibration/tracking_research.yaml)|[报告](local_world-nominal_research-holdout/verification.md) / [CSV](local_world-nominal_research-holdout/holdout_errors.csv)|[summary](local_world-nominal_research-replay/summary.yaml) / [tracks](local_world-nominal_research-replay/tracks.yaml)|12451|0.064506477|
|local_world-bounded_only|[calibration](local_world-bounded_only-calibration/calibration.md) / [profile](local_world-bounded_only-calibration/profile.yaml) / [研究配置](local_world-bounded_only-calibration/tracking_research.yaml)|[报告](local_world-bounded_only-holdout/verification.md) / [CSV](local_world-bounded_only-holdout/holdout_errors.csv)|[summary](local_world-bounded_only-replay/summary.yaml) / [tracks](local_world-bounded_only-replay/tracks.yaml)|12381|0.077786546|

每域12849帧，128个注入坏测量全部拒绝（含未确认seed），G5资格0。各域文件夹有配置、全部日志、6项整体统计/12项session分层，以及全部3个holdout session的逐帧tracks。速度3σ球经验包含率不足，未调门槛；MEASURED为72.11%–91.46%，COASTING为56.94%–75.30%。

## 跨域、旧G2、错误输入

- [33ms配置](configs/probe-33ms.yaml)、[生成](generate-33ms.log)、[verify退出1及原因](probe-33ms.log)、[报告](probe-33ms/verification.md)、[CSV](probe-33ms/holdout_errors.csv)、[原14ms profile前后hash](probe-profile-hash.json)。5458帧，零速度/零共同样本。51坏测量中47个未确认seed被当新测量（已确认接受0），该跨域关联失败保留。
- [旧G2输入SHA256/定位](legacy-g2-input.sha256)、[原summary副本](legacy-g2-input-summary.yaml)、[G4退出2](legacy-g2.log)、[summary](legacy-g2/summary.yaml)、[tracks](legacy-g2/tracks.yaml)。1676帧、raw MARK1088、Unique0，G4实测/速度/资格0；全部曝光/时间误差保持空。
- [异CWD检查](other-cwd.log)、[负例输入](negative-inputs/)、[空时间界命名错误](negative-bound-empty.log)、[缺字段命名错误](negative-bound-key-missing.log)、[显式零界合法](bound-explicit-zero.log)。11个CLI负例均exit1且原因匹配，原IO I01–I09和构造验证回归全过。

## 本地持久包与复跑

[最终ZIP](g4-delivery.zip)、[ZIP SHA256](g4-delivery.zip.sha256)、[打包报告](package-report.json)、[最终包校验](archive-audit.json)。ZIP为打包时证据快照，包含全量14ms/33ms原始输入/truth、完整日志与报告、20文件及集成源代码快照/patch；不含build、第三方库或凭据。包外命令记录还包含打包/包校验的最后退出码；archive-inventory.sha256按ZIP条目而非更新后的包外commands.jsonl解释。

构建目录保留build/g4-release、build/g4-debug、build/g4-asan供复验。依据第6–7节用新的证据/输出目录复跑，禁止覆盖现有产物。reproduction目录保存本次辅助脚本，原命令以commands.jsonl为准，输出均拒绝覆盖；step5脚本在沙箱首轮LeakSanitizer失败处停止，沙箱外重跑命令单列。

未解决：G-01真实Unique链、G-02速度误差包含与G5物理预算、G-03硬件曝光时间界；33ms关联未确认seed误接受及完全重合/遮挡实例身份仍按现有适用边界保留。关闭流程见[g4.md](../../g4.md#后续关闭流程)。
