from pathlib import Path
import datetime
import json

root = Path(__file__).resolve().parents[6]
E = root / "src/tushenghao/docs/evidence/g4"
S = json.loads((E / "results-final.json").read_text())
commands = [json.loads(line) for line in (E / "commands.jsonl").read_text().splitlines()]
now = datetime.datetime.now(datetime.timezone.utc)
start = datetime.datetime.fromisoformat((E / "start-time.txt").read_text().strip())
resume = datetime.datetime.fromisoformat(json.loads((E / "resume-check.json").read_text())["time"])
command_s = sum(c["duration_s"] for c in commands)
stamp = now.astimezone(datetime.timezone(datetime.timedelta(hours=8))).isoformat(timespec="seconds")
timing = dict(as_of=now.isoformat(), baseline_started=start.isoformat(), resumed_at=resume.isoformat(),
              elapsed_since_baseline_s=(now-start).total_seconds(), elapsed_since_resume_s=(now-resume).total_seconds(),
              cumulative_recorded_command_s=command_s, note="自然历时含中断；命令累计耗时不是独占CPU或人工工时")
(E / "timing.json").write_text(json.dumps(timing, ensure_ascii=False, indent=2) + "\n")
rows, coverage, replays, comparison = [], [], [], []
historical = [(12611, .057741), (12481, .069802), (12451, .064506), (12381, .077787)]
for (tag, domain), (old_n, old_rmse) in zip(S["domains"].items(), historical):
    c, m = domain["counts"], domain["metrics"]["TargetTracker全部可用输出"]
    pair = domain["metrics"]["共同样本tracker"]
    rows.append(f"|{tag}|{c['frames']}|128/128|{m['samples']}|{m['velocity_rmse_mps']:.9f}|{m['velocity_max_error_mps']:.9f}|{pair['samples']}|{c['recovered']}/{c['reversals']}|")
    v = domain["coverage"]
    coverage.append(f"|{tag}|{v['MEASURED']['velocity_contained_over_n']} ({v['MEASURED']['velocity_ratio']:.2%})|{v['COASTING']['velocity_contained_over_n']} ({v['COASTING']['velocity_ratio']:.2%})|")
    r = domain["replay"]
    replays.append(f"|{tag}|{r['frames']}|{r['measured']}|{r['coasting']}|{r['lost']}|{r['velocity_output_frames']}|{float(r['tracking_step_mean_us']):.3f}|{float(r['tracking_step_p95_us']):.3f}|")
    comparison.append(f"|{tag}|{old_n} / {old_rmse:.6f}|{m['samples']} / {m['velocity_rmse_mps']:.9f}|{m['samples']-old_n} / {m['velocity_rmse_mps']-old_rmse:+.9f}|")
metrics_table = "|域|holdout帧|坏量测拒绝|速度样本|速度RMSE(m/s)|最大速度误差(m/s)|共同样本|反转恢复|\n|---|---:|---:|---:|---:|---:|---:|---:|\n" + "\n".join(rows)
coverage_table = "|域|MEASURED速度3σ球包含/样本|COASTING速度3σ球包含/样本|\n|---|---:|---:|\n" + "\n".join(coverage)
replay_table = "|域|帧|MEASURED|COASTING|LOST|速度帧|step均值(μs)|step p95(μs)|\n|---|---:|---:|---:|---:|---:|---:|---:|\n" + "\n".join(replays)
comparison_table = "|域|文档隔离复核：样本/RMSE|本地重跑：样本/RMSE|差值：样本/RMSE|\n|---|---:|---:|---:|\n" + "\n".join(comparison)

completed = [
    "完成；5095个初始文件仅指定三处追加，G2零差异；见[evidence/g4/freeze-check.json](evidence/g4/freeze-check.json)",
    "完成；20文件落盘、双构建、R2数值断言通过；见[evidence/g4/r2-and-review.md](evidence/g4/r2-and-review.md)",
    "M01–M12/T01–T51在Release/Debug/ASan均通过",
    "四域独立完成，每域6项总体/12项分层统计、12849帧holdout、128坏量测全部拒绝",
    "I01–I09通过；11个CLI负例命名拒绝，显式零界合法；三处导航已追加",
]
verified = [
    "Release=29/29（9.82s），Debug=29/29（50.62s）；配置/构建/CTest退出0，原测试未修改",
    "全通过；R2 Ppp_xx直接断言期望0.0206987654320988m²、容差1e-12；缺界验证/构造命名拒绝、零界合法回归通过",
    "Release/Debug/ASan均通过；大整数、空帧、旧格式保持；旧1676帧曝光/时间误差全部空",
    "3 train/2 selection/3 holdout完整会话，四域各自profile；每项n/μ/std/3σ/来源及12项分层均已核对，holdout不调参",
    "raw/一阶/KF全部及共同样本、覆盖、反转、经验包含率、全量CSV和tracks均保留；速度包含率不足如实记于下表",
    "四域calibrate/verify/replay均退出0且有速度，跨域借用profile负例退出1并命名报错",
    "/tmp检查退出0；11个CLI负例退出1且原因匹配；显式零时间界退出0；所有合成回放G5资格0",
    "原1676帧/raw MARK=1088/Unique=0；G4推进1676 tick、实测/速度/资格0，退出2；输入及相邻summary hash归档",
    "G-01保持OPEN；BudgetMissing及两个空预算保持，不取候选补测量",
]
path = root / "src/tushenghao/docs/g4_acceptance.md"
lines = path.read_text().splitlines()
ci = vi = 0
for i, line in enumerate(lines):
    if "未执行（由本地执行结果更新）" not in line:
        continue
    cells = line.split("|")
    if cells[1].strip() == "未执行（由本地执行结果更新）":
        lines[i] = "| bounded_only缺profile时间界 | 原validator允许空optional、调用裸.value() | 按用户审查补G4_PROFILE_BOUNDED_TIME_BOUND_MISSING并移除裸.value()；有限非负语义不变 | 验证/构造主动断言及空值/缺key/负数/NaN/零界CLI均验证 |"
    elif ci < len(completed):
        cells[2] = " " + completed[ci] + " "
        lines[i] = "|".join(cells)
        ci += 1
    else:
        cells[-2] = " " + verified[vi] + " "
        lines[i] = "|".join(cells)
        vi += 1
assert ci == 5 and vi == 9
text = "\n".join(lines) + "\n"
old = "实际耗时：阶段／实际投入／验证结果／剩余项／是否需用户决策。缺文档或前置关卡未关闭，不写“实车G4验收通过”。"
assert old in text
text = text.replace(old, f"实测更新：{stamp}。初始基线至记录自然历时{(now-start).total_seconds()/60:.2f}分钟（含中断），恢复后{(now-resume).total_seconds()/60:.2f}分钟；验收命令累计{command_s:.3f}s。各阶段实际命令耗时/退出码见[commands.jsonl](evidence/g4/commands.jsonl)及[timing.json](evidence/g4/timing.json)。软件施工与离线流程完成；剩余三关保持OPEN，当前不需修改算法或冻结G2的决策。没有实车证据，不写“实车G4验收通过”。")
details = """
## 本次内存检查与跨域探针

ASan/UBSan构建退出0。首轮沙箱CTest退出8，原因是LeakSanitizer不支持ptrace；按环境要求经授权在沙箱外重跑同构建、同参数（detect_leaks=1、halt_on_error=1），2/2通过，退出0且无ASan/UBSan/泄漏报告。两轮日志均保留，未关闭泄漏检查。[执行说明](evidence/g4/memory-check-note.md)。

33ms独立种子输入5458帧，沿用冻结14ms profile，速度/共同样本=0，退出1且原因匹配；profile前后SHA256相同。51个注入中47个被当未确认新测量seed、已确认错误接受=0；该跨域关联失败保留，不调整max_dt或增加门控。

## 四域独立holdout实测

""" + metrics_table + "\n\n" + coverage_table + """

经验速度包含率仍不足，不按已见holdout改P/门槛，G-02保持OPEN。各域位置包含率、raw/一阶/KF全部及共同样本、反转恢复和逐帧失败完整见verification.md/holdout_errors.csv。

## 首个holdout会话回放与耗时

""" + replay_table + """

四域输入拒绝/G5资格均为0；step耗时仅含tracker.step，不含解析、写盘或上游，硬件/源完整性资格未授予。

证据目录及最终ZIP见[evidence/g4/INDEX.md](evidence/g4/INDEX.md)，包含全session输入/truth、源码快照与完整命令日志，不含build或第三方库。
"""
path.write_text(text + details)
g4 = root / "src/tushenghao/docs/g4.md"
text = g4.read_text()
assert "## 本次本地施工实测" not in text
text += "\n## 本次本地施工实测\n\n" + stamp + "，feat/tracking / 30ac7ab。20文件按执行依据落盘；CMake/README/INDEX指定追加，原Detector/G2及原配置/接口/测试保持，用户已暂存执行文档未改，未做Git写操作。\n\n"
text += "Release=29/29、Debug=29/29；M01–M12/T01–T51/I01–I09及R2直接数值断言均通过。ASan/UBSan首轮因沙箱ptrace退出8；经授权沙箱外原参数重跑2/2，退出0且无报告，首轮失败日志保留。\n\n"
text += "按审查备注，bounded_only profile缺validated_time_bound_s在validate_tracking_profile中报G4_PROFILE_BOUNDED_TIME_BOUND_MISSING；初始化/重建调用不再裸取.value()。空界/缺key/负数/NaN均拒绝，显式零界合法，验证及构造回归通过。\n\n" + metrics_table + "\n\n"
text += "33ms独立输入5458帧，沿用14ms profile，零速度/零共同样本、退出1；51个注入中47个未确认位置seed被当新测量、已确认错误接受0，失败保留。旧G2回放1676帧/raw MARK=1088/Unique=0，G4零实测/零速度/零资格、退出2；曝光和时间误差保持空。/tmp检查退出0；11个CLI负例命名退出1，显式零界退出0。\n\n"
text += "速度经验包含率仍不足，全部失败/未恢复反转均保留。G-01/G-02/G-03保持OPEN；软件结果不授予实车或投放资格。完整数据、逐命令退出码、冻结检查及ZIP见[证据索引](evidence/g4/INDEX.md)和[本地验收记录](g4_acceptance.md)，原后续关闭流程保留。\n"
g4.write_text(text)
r2 = """# R2与审查备注逐文件记录

依据仅docs/ref/G4-Codex.md及用户审查备注。首次与恢复时20/20文件均逐字匹配第4节，恢复时未重写完整文件。

- lib/tracking/kalman_cv.hpp/cpp：按文档原样落盘R2的内部position_time_bound_s参数；有界Ppp=2*(R1+Bpos²*(v v′+velocity_mse))，Pvv、精确时间分支及门槛不变。
- lib/tracking/target_tracker.cpp：按文档两处bootstrap分别传Bdt及profile单帧Bpos；按用户审查去掉两处裸.value()，构造验证及time_bounded的非空契约保护解引用。
- tools/validation/tracking_lab.cpp：按文档先从selection预读单帧界统计，再传bootstrap；已赋值optional改解引用，数值与来源未变。
- lib/config/tracking_config.cpp：按审查补bounded_only缺界错误G4_PROFILE_BOUNDED_TIME_BOUND_MISSING；有限非负语义保持，0合法，nominal_research可以未声明。
- tests/tracking_contract_test.cpp：保留文档R2数值断言，追加缺界验证/TargetTracker构造命名拒绝与显式零界合法回归。旧fixture/断言不变，目标数仍29。
- docs/g4.md/g4_acceptance.md：记录本轮实测及失败，保留后续关闭流程与三关OPEN。

## R2直接断言

R0=R1=0.01I、dt=0.2s、Bdt=0.02s、Bpos=0.01s、v=(1,0.5,0)m/s、q=0的fixture：velocity_mse_xx=2.493827160493827，原有界Ppp_xx=0.02m²，新期望0.0206987654320988m²，补项0.0006987654320988m²。Release/Debug/ASan主动near断言通过，容差1e-12；精确时间M10仍直接断言Ppp=0.04m²。测试数值未进入生产配置。

""" + json.dumps(S["r2_fixture"], ensure_ascii=False, indent=2) + """

## 文档历史复核与本地重跑差异

""" + comparison_table + """

历史表为文档第8节编写者隔离复核，仅给六位小数；本地数据由本轮generate/calibrate/verify重新形成，样本一致，RMSE六位小数一致。差值仅相对于公开精度，不是改善结论。仓库没有本轮可用的R2前完整执行证据，不伪造旧报告或声称重跑旧算法；同fixture可直接复核补项前后。四域错误接受0、包含率不足、33ms未确认seed错误接受及沙箱内存首轮失败均如实保留。
"""
(E / "r2-and-review.md").write_text(r2)
print("g4.md / g4_acceptance.md: local measurements, failures, timing and OPEN gates recorded; R2/review evidence written")
