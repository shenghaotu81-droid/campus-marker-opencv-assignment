from pathlib import Path
import csv, hashlib, json, re, shutil, subprocess, sys
root=Path(__file__).resolve().parents[6]
runner=root/"src/tushenghao/docs/evidence/g4/reproduction/run_command.py"
E="src/tushenghao/docs/evidence/g4"
def run(label,command,expect=0,extra=()):
    subprocess.run([sys.executable,str(runner),"--label",label,"--expect",str(expect),*extra,"--",*command],cwd=root,check=True)
profile=root/E/"camera_relative-nominal_research-calibration/profile.yaml"
frozen_profile_hash=hashlib.sha256(profile.read_bytes()).hexdigest()
run("probe-33ms-config",["sed","-e","s/nominal_dt_s: 0.014/nominal_dt_s: 0.033/","-e","s/2026102901/2026202901/","-e","s/2026103901/2026203901/","-e","s/2026104901/2026204901/","src/tushenghao/config/tracking_lab.yaml"],extra=["--log","configs/probe-33ms.yaml"])
run("generate-33ms",["build/g4-release/tracking_lab","generate","--config",E+"/configs/probe-33ms.yaml","--out",E+"/data-33ms"])
run("probe-33ms",["build/g4-release/tracking_lab","verify","--config",E+"/configs/probe-33ms.yaml","--input",E+"/data-33ms","--profile",E+"/camera_relative-nominal_research-calibration/profile.yaml","--out",E+"/probe-33ms"],1)
with (root/E/"probe-33ms.log").open("a") as f: f.write("exit=1\n")
report=(root/E/"probe-33ms/verification.md").read_text()
for row in ["TargetTracker全部可用输出","共同样本tracker"]:
    assert re.search(r"^\|"+row+r"\|0\|",report,re.M), "STOP: 33ms unexpectedly has velocity/common samples"
assert "无速度输出或无共同对照样本" in (root/E/"probe-33ms.log").read_text()
assert frozen_profile_hash==hashlib.sha256(profile.read_bytes()).hexdigest()
(root/E/"probe-profile-hash.json").write_text(json.dumps({"before":frozen_profile_hash,"after":hashlib.sha256(profile.read_bytes()).hexdigest()},indent=2)+"\n")
poses=Path("src/tushenghao/docs/evidence/g2/runs/g2-video-001/observations.yaml")
assert (root/poses).is_file(), "STOP: required frozen G2 observations missing"
summary=root/poses.parent/"summary.yaml"
assert summary.is_file()
shutil.copyfile(summary,root/E/"legacy-g2-input-summary.yaml")
(root/E/"legacy-g2-input.sha256").write_text("".join(hashlib.sha256(p.read_bytes()).hexdigest()+"  "+str(p.relative_to(root))+"\n" for p in [root/poses,summary]))
run("legacy-g2",["build/g4-release/tracking_app","--input",str(poses),"--profile",E+"/camera_relative-nominal_research-calibration/profile.yaml","--out",E+"/legacy-g2"],2)
with (root/E/"legacy-g2.log").open("a") as f: f.write("exit=2\n")
run("other-cwd",[str(root/"build/g4-release/tracking_app"),"--check-config"],extra=["--cwd","/tmp"])
print("33ms zero velocity/common samples exit=1; profile unchanged; legacy exit=2; other CWD exit=0",flush=True)
