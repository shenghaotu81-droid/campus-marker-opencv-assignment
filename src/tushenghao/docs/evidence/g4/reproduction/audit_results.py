from pathlib import Path
import csv, hashlib, json, math, re
root=Path(__file__).resolve().parents[6]
E=root/"src/tushenghao/docs/evidence/g4"
def flat(path):
    result={}
    for line in path.read_text().splitlines():
        m=re.fullmatch(r'([A-Za-z_][A-Za-z_0-9]*):\s*(.*)',line)
        if m: result[m[1]]=m[2].strip().strip('"')
    return result
summary={"domains":{},"checks":{},"gates":{"G-01":"OPEN","G-02":"OPEN","G-03":"OPEN"}}
for mode in ["camera_relative","local_world"]:
    for policy in ["nominal_research","bounded_only"]:
        tag=mode+"-"+policy
        calibration=E/(tag+"-calibration")
        report=(E/(tag+"-holdout/verification.md")).read_text()
        profile=flat(calibration/"profile.yaml")
        assert profile["coordinate_mode"]==mode and profile["time_policy"]==policy
        assert profile["hardware_verified"]=="0" and profile["camera_id"]=="synthetic-camera-g4"
        metrics={}
        coverage={}
        for line in report.splitlines():
            cells=line.strip('|').split('|')
            if line.startswith('|') and len(cells)==5:
                if cells[0] in ["raw有限差分","时间感知一阶平滑","TargetTracker全部可用输出","共同样本raw","共同样本平滑","共同样本tracker"]:
                    metrics[cells[0]]={"samples":int(cells[1]),"position_rmse_m":float(cells[2]),"velocity_rmse_mps":float(cells[3]),"velocity_max_error_mps":float(cells[4])}
                elif cells[0] in ["COASTING","MEASURED"]:
                    coverage[cells[0]]={"position_contained_over_n":cells[1],"position_ratio":float(cells[2]),"velocity_contained_over_n":cells[3],"velocity_ratio":float(cells[4])}
        assert len(metrics)==6 and len(coverage)==2
        total=re.search(r'总帧=(\d+)，MEASURED=(\d+)，COASTING=(\d+)，LOST=(\d+)',report)
        wrong=re.search(r'错误测量注入=(\d+)，被当新测量接受=(\d+)（其中已确认track接受=(\d+)',report)
        reverse=re.search(r'世界目标实际反转=(\d+)，在profile条件速度尺度内恢复=(\d+)',report)
        assert total and wrong and reverse
        counts=dict(zip(["frames","measured","coasting","lost"],map(int,total.groups())))
        counts.update(dict(zip(["injected","wrong_accepted_all","wrong_accepted_confirmed"],map(int,wrong.groups()))))
        counts.update(dict(zip(["reversals","recovered"],map(int,reverse.groups()))))
        assert counts["frames"]==12849 and counts["injected"]==128 and counts["wrong_accepted_all"]==0
        assert "G5资格输出=0" in report
        with (E/(tag+"-holdout/holdout_errors.csv")).open() as f: errors=list(csv.DictReader(f))
        assert len(errors)==12849 and len({r["session"] for r in errors})==3
        assert sum(bool(r["velocity_error_mps"]) for r in errors)==metrics["TargetTracker全部可用输出"]["samples"]
        stats=[]; layers=[]
        for line in (calibration/"calibration.md").read_text().splitlines():
            cells=line.strip('|').split('|')
            if not line.startswith('|'): continue
            if len(cells)==6 and cells[0] in ["association_nis","bootstrap_nis","position_std_m","velocity_std_mps","max_dt_s","time_bound_reference_s"]:
                item={"statistic":cells[0],"n":int(cells[1]),"mean":float(cells[2]),"sample_std":float(cells[3]),"threshold":float(cells[4]),"source":cells[5]}
                assert item["n"]>=2 and math.isclose(item["mean"]+3*item["sample_std"],item["threshold"],rel_tol=1e-12,abs_tol=1e-16)
                assert item["source"]==profile["evidence_id"]
                stats.append(item)
            elif len(cells)==6 and cells[0].startswith('selection-seed-'):
                assert int(cells[2])>=2
                assert math.isclose(float(cells[3])+3*float(cells[4]),float(cells[5]),rel_tol=1e-12,abs_tol=1e-16)
                layers.append(cells)
        assert len(stats)==6 and len(layers)==12
        replay=flat(E/(tag+"-replay/summary.yaml"))
        assert int(replay["frames"])==4287 and int(replay["velocity_output_frames"])>0
        assert int(replay["qualified_for_g5"])==0 and int(replay["rejected_inputs"])==0
        summary["domains"][tag]={"counts":counts,"metrics":metrics,"coverage":coverage,"statistics":stats,"session_layers":layers,"profile":profile,"replay":replay}
        print(tag, json.dumps({"counts":counts,"tracker":metrics["TargetTracker全部可用输出"],"replay_velocity":replay["velocity_output_frames"]},ensure_ascii=False))
for variant in ["release","debug"]:
    assert "100% tests passed, 0 tests failed out of 29" in (E/("ctest-"+variant+".log")).read_text()
    active=(E/("last-test-"+variant+".log")).read_text()
    assert "G4 tracking contract M01-M12 / T01-T51 PASS" in active and "G4 IO I01-I09 PASS" in active
    assert "G4 bounded profile missing-bound / explicit-zero PASS" in active
assert "100% tests passed, 0 tests failed out of 2" in (E/"ctest-asan-unsandboxed.log").read_text()
assert "LeakSanitizer has encountered a fatal error" in (E/"ctest-asan.log").read_text()
asan=(E/"ctest-asan-unsandboxed.log").read_text()+(E/"last-test-asan-unsandboxed.log").read_text()
assert not re.search(r'AddressSanitizer|LeakSanitizer|runtime error:|SUMMARY: .*Sanitizer',asan)
legacy=flat(E/"legacy-g2/summary.yaml")
assert int(legacy["frames"])==1676
for key in ["measured","velocity_output_frames","qualified_for_g5","rejected_inputs"]: assert int(legacy[key])==0
legacy_tracks=(E/"legacy-g2/tracks.yaml").read_text()
assert len(re.findall(r'^\s+input_accepted: 1$',legacy_tracks,re.M))==1676
assert len(re.findall(r'^\s+exposure_us: ""$',legacy_tracks,re.M))==1676
assert len(re.findall(r'^\s+time_uncertainty_s: ""$',legacy_tracks,re.M))==1676
assert len(re.findall(r'^\s+tracks:\n\s+\[\]$',legacy_tracks,re.M))==1676
probe=(E/"probe-33ms/verification.md").read_text()
assert re.search(r'^\|TargetTracker全部可用输出\|0\|',probe,re.M)
assert re.search(r'^\|共同样本tracker\|0\|',probe,re.M)
assert '无速度输出或无共同对照样本' in (E/"probe-33ms.log").read_text()
probe_counts=re.search(r'总帧=(\d+)，MEASURED=(\d+)，COASTING=(\d+)，LOST=(\d+)',probe)
probe_wrong=re.search(r'错误测量注入=(\d+)，被当新测量接受=(\d+)（其中已确认track接受=(\d+)',probe)
summary["probe33"]={"frames":int(probe_counts[1]),"injected":int(probe_wrong[1]),"wrong_accepted_all":int(probe_wrong[2]),"wrong_accepted_confirmed":int(probe_wrong[3]),"velocity_samples":0,"paired_samples":0,"exit_code":1}
summary["legacy"]=legacy
summary["checks"]={"release":"29/29","debug":"29/29","memory_initial_exit":8,"memory_unsandboxed":"2/2; exit=0; no sanitizer report","negative_cli_cases":11,"explicit_zero_bound":"accepted; exit=0"}
summary["r2_fixture"]={"r0_and_r1_diagonal_m2":0.01,"dt_s":0.2,"Bdt_s":0.02,"Bpos_s":0.01,"velocity_mse_x_m2ps2":4*0.02/(0.18*0.18)+2*0.02*0.02/(0.18*0.18),"old_Ppp_xx_m2":0.02,"expected_Ppp_xx_m2":2*(0.01+0.01*0.01*(1+4*0.02/(0.18*0.18)+2*0.02*0.02/(0.18*0.18))),"assertion_tolerance":1e-12,"direct_assertion":"Release/Debug/ASan contract passed"}
(E/"results-final.json").write_text(json.dumps(summary,ensure_ascii=False,indent=2)+"\n")
print('Audit: counts, six thresholds and 12 session strata per domain, profiles, replay, active assertions, memory, legacy ticks, probe PASS')
