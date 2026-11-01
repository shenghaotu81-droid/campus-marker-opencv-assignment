# 中断恢复与辅助步骤记录

- 初次只读核对确认feat/tracking和30ac7ab，初始仅G4-Codex.md由用户预先暂存。初次误以仓库根有CMakeLists/cmake，两个只读查找退出2；随后核对文档第5/7节确认实际构建源为src/tushenghao，未更改仓库布局。
- Step0基线与5095文件hash已记录；20文件首次按第4节完整代码块落盘（退出0）。用户中断后，逐项20/20完全一致、原文件5095/5095未变、G2 diff空、基线证据完好，再从集成和审查备注处继续，未重复落盘。
- 命令记录器首次路径自检退出1（parents索引偏一）；仅修正证据辅助脚本根路径，随后所有验收从仓库根运行。
- 文档辅助脚本构造时引号错误退出1，尚未生成目标脚本；随后的update-docs命令退出2，未写入源码或文档。修正生成方式后update-docs-final退出0。错误日志保留，实测结果与产品代码不受影响。
- 冻结保护辅助检查首轮退出1，发现README在指定插入之外被编辑工具增加末尾换行；仅恢复其原有无末尾换行字节形式。复核移除指定追加后SHA256与Step0完全相同，finalize-sources-final退出0。after.patch为该EOF修正前的中间快照，after-final.patch为最终权威快照，两者均保留；delivery.patch和源码快照均是最终字节。
- ASan沙箱首轮CTest退出8，按环境要求经用户授权在沙箱外同参数重跑退出0，未关闭detect_leaks；详见memory-check-note.md。
- 源码/配置没有为换绿而重设计、调参或修改冻结G2；未执行checkout/reset/add/commit/push/merge等Git写操作。所有既有文件最终仅三处指定追加，用户暂存文档逐字节保留。
