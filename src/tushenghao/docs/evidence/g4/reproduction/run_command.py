import argparse, datetime, json, os, pathlib, shlex, subprocess, sys, time
ROOT = pathlib.Path(__file__).resolve().parents[6]
E = ROOT / "src/tushenghao/docs/evidence/g4"
p = argparse.ArgumentParser()
p.add_argument("--label", required=True)
p.add_argument("--log")
p.add_argument("--expect", type=int, default=0)
p.add_argument("--cwd")
p.add_argument("--env", action="append", default=[])
p.add_argument("command", nargs=argparse.REMAINDER)
a = p.parse_args()
command = a.command[1:] if a.command[:1] == ["--"] else a.command
assert command
cwd = pathlib.Path(a.cwd) if a.cwd else ROOT
log = E / (a.log or (a.label + ".log"))
env = os.environ.copy()
overrides = dict(item.split("=", 1) for item in a.env)
env.update(overrides)
started = datetime.datetime.now(datetime.timezone.utc).isoformat()
begin = time.monotonic()
with log.open("xb") as f:
    result = subprocess.run(command, cwd=cwd, env=env, stdout=f, stderr=subprocess.STDOUT)
record = dict(label=a.label, command=command, shell_display=shlex.join(command), cwd=str(cwd),
              env=overrides, exit_code=result.returncode, expected_exit_code=a.expect,
              started_utc=started, duration_s=time.monotonic()-begin, log=str(log.relative_to(ROOT)))
with (E / "commands.jsonl").open("a") as f:
    f.write(json.dumps(record, ensure_ascii=False) + "\n")
print(f"{a.label}: exit={result.returncode}, expected={a.expect}, elapsed={record['duration_s']:.3f}s, log={record['log']}", flush=True)
if result.returncode != a.expect:
    print(log.read_text(errors="replace")[-12000:])
sys.exit(0 if result.returncode == a.expect else 1)
