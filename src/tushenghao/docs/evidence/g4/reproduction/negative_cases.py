from pathlib import Path
import re, subprocess, sys
root=Path(__file__).resolve().parents[6]
E="src/tushenghao/docs/evidence/g4"
runner=root/E/"reproduction/run_command.py"
fixtures=root/E/"negative-inputs"
fixtures.mkdir(exist_ok=False)
empty="%YAML:1.0\n---\nschema_version: 4\nstream_type: G4PoseReplay\nposition_unit: m\ntime_unit: us\nframes: []\n"
(fixtures/"empty.yaml").write_text(empty)
(fixtures/"schema99.yaml").write_text(empty.replace("schema_version: 4","schema_version: 99"))
bounded=(root/E/"camera_relative-bounded_only-calibration/profile.yaml").read_text()
for suffix,value in [("empty",'""'),("zero","0."),("negative","-0.001"),("nan",".Nan")]:
    body=re.sub(r"^validated_time_bound_s:.*$","validated_time_bound_s: "+value,bounded,flags=re.M)
    assert body!=bounded
    (fixtures/("bound-"+suffix+".yaml")).write_text(body)
(fixtures/"bound-key-missing.yaml").write_text(re.sub(r"^validated_time_bound_s:.*\n","",bounded,flags=re.M))
def run(label,args,code,reason):
    subprocess.run([sys.executable,str(runner),"--label",label,"--expect",str(code),"--",*args],cwd=root,check=True)
    text=(root/E/(label+".log")).read_text()
    assert reason in text, (label,"STOP: wrong rejection reason",text)
    assert "bad_optional_access" not in text
app="build/g4-release/tracking_app"
profile=E+"/camera_relative-nominal_research-calibration/profile.yaml"
for label,args,reason in [
    ("unknown-option",["--check-config","--unknown"],"未知/重复/缺值参数"),
    ("duplicate-option",["--check-config","--check-config"],"重复check-config"),
    ("missing-value",["--check-config","--profile"],"未知/重复/缺值参数"),
    ("empty-input",["--input",E+"/negative-inputs/empty.yaml","--profile",profile,"--out",E+"/negative-empty-output"],"G4_REPLAY_EMPTY_INPUT_REJECTED"),
    ("schema-input",["--input",E+"/negative-inputs/schema99.yaml","--profile",profile,"--out",E+"/negative-schema-output"],"G4_REPLAY_UNSUPPORTED_VERSION"),
    ("bound-empty",["--check-config","--profile",E+"/negative-inputs/bound-empty.yaml"],"G4_PROFILE_BOUNDED_TIME_BOUND_MISSING"),
    ("bound-key-missing",["--check-config","--profile",E+"/negative-inputs/bound-key-missing.yaml"],"G4_CONFIG_MISSING:validated_time_bound_s"),
    ("bound-negative",["--check-config","--profile",E+"/negative-inputs/bound-negative.yaml"],"G4_PROFILE_NO_PROVENANCE_OR_TIME_BOUND"),
    ("bound-nan",["--check-config","--profile",E+"/negative-inputs/bound-nan.yaml"],"G4_CONFIG_NONFINITE"),
    ("existing-output",["--input",E+"/data-14ms/holdout/holdout-seed-2026104901/frames.yaml","--profile",profile,"--out",E+"/camera_relative-nominal_research-replay"],"拒绝覆盖现有输出目录")]:
    run("negative-"+label,[app,*args],1,reason)
run("bound-explicit-zero",[app,"--check-config","--profile",E+"/negative-inputs/bound-zero.yaml"],0,"G4配置合法")
run("negative-profile-domain",["build/g4-release/tracking_lab","verify","--config",E+"/configs/camera_relative-bounded_only.yaml","--input",E+"/data-14ms","--profile",profile,"--out",E+"/negative-profile-domain"],1,"profile坐标/时间域不匹配")
print("11 negative CLI cases exit=1 with expected reason; explicit zero exit=0; no bad_optional_access",flush=True)
