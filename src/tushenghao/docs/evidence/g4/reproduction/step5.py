from pathlib import Path
import subprocess, sys
root=Path(__file__).resolve().parents[6]
runner=root/"src/tushenghao/docs/evidence/g4/reproduction/run_command.py"
E=root/"src/tushenghao/docs/evidence/g4"
def run(label,command,extra=()):
    subprocess.run([sys.executable,str(runner),"--label",label,*extra,"--",*command],cwd=root,check=True)
run("ctest-release",["ctest","--test-dir","build/g4-release","--output-on-failure"])
for variant in ["release"]:
    (E/("last-test-"+variant+".log")).write_bytes((root/("build/g4-"+variant+"/Testing/Temporary/LastTest.log")).read_bytes())
run("build-debug",["cmake","--build","build/g4-debug","-j4"])
run("ctest-debug",["ctest","--test-dir","build/g4-debug","--output-on-failure"])
(E/"last-test-debug.log").write_bytes((root/"build/g4-debug/Testing/Temporary/LastTest.log").read_bytes())
run("check-config",["build/g4-release/tracking_app","--check-config"])
run("build-asan",["cmake","--build","build/g4-asan","--target","tracking_contract_test","tracking_io_test","-j4"])
run("ctest-asan",["ctest","--test-dir","build/g4-asan","-R","^tracking_(contract|io)_test$","--output-on-failure"], ["--env","ASAN_OPTIONS=detect_leaks=1","--env","UBSAN_OPTIONS=halt_on_error=1"])
(E/"last-test-asan.log").write_bytes((root/"build/g4-asan/Testing/Temporary/LastTest.log").read_bytes())
