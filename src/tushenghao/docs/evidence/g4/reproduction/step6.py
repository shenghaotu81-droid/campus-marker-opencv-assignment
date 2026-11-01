from pathlib import Path
import csv, json, re, subprocess, sys
root=Path(__file__).resolve().parents[6]
runner=root/"src/tushenghao/docs/evidence/g4/reproduction/run_command.py"
E="src/tushenghao/docs/evidence/g4"
def run(label, command, extra=()):
    subprocess.run([sys.executable,str(runner),"--label",label,*extra,"--",*command],cwd=root,check=True)
(root/E/"configs").mkdir(exist_ok=False)
run("generate-14ms",["build/g4-release/tracking_lab","generate","--out",E+"/data-14ms"])
for mode in ["camera_relative","local_world"]:
    for policy in ["nominal_research","bounded_only"]:
        tag=mode+"-"+policy
        config=E+"/configs/"+tag+".yaml"
        run(tag+"-config",["sed","-e",'s/tracking_coordinate_mode: "camera_relative"/tracking_coordinate_mode: "'+mode+'"/',"-e",'s/tracking_time_policy: "nominal_research"/tracking_time_policy: "'+policy+'"/',"src/tushenghao/config/tracking_lab.yaml"], ["--log","configs/"+tag+".yaml"])
        run(tag+"-calibrate",["build/g4-release/tracking_lab","calibrate","--config",config,"--input",E+"/data-14ms","--out",E+"/"+tag+"-calibration"])
        run(tag+"-verify",["build/g4-release/tracking_lab","verify","--config",config,"--input",E+"/data-14ms","--profile",E+"/"+tag+"-calibration/profile.yaml","--out",E+"/"+tag+"-holdout"])
        report=(root/E/(tag+"-holdout/verification.md")).read_text()
        assert "总帧=12849" in report, "STOP: holdout frame count differs from document"
        assert "错误测量注入=128，被当新测量接受=0" in report, "STOP: injected/accepted count differs from document"
        assert "G5资格输出=0" in report
        row=re.search(r"^\|TargetTracker全部可用输出\|(\d+)\|",report,re.M)
        assert row and int(row[1])>0, "STOP: no holdout velocity"
        with (root/E/(tag+"-holdout/holdout_errors.csv")).open() as f:
            rows=list(csv.DictReader(f))
        assert len(rows)==12849 and len({r["session"] for r in rows})==3
        run(tag+"-replay",["build/g4-release/tracking_app","--config",E+"/"+tag+"-calibration/tracking_research.yaml","--input",E+"/data-14ms/holdout/holdout-seed-2026104901/frames.yaml","--out",E+"/"+tag+"-replay"])
        print(tag+": document counts 12849 frames / 128 injections / 0 accepted verified",flush=True)
