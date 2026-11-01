from pathlib import Path
import csv
import hashlib
import json
import math
import re
from urllib.parse import unquote

root = Path(__file__).resolve().parents[6]
E = root / "src/tushenghao/docs/evidence/g4"
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
files = (E / "modified-files.txt").read_text().splitlines()
assert len(files) == 23
for p in files:
    assert sha(root / p) == sha(E / "source-snapshot" / p), p
for line in (E / "modified-files.sha256").read_text().splitlines():
    h, p = line.split("  ", 1)
    assert sha(root / p) == h, p
for line in (E / "raw-data.sha256").read_text().splitlines():
    h, p = line.split("  ", 1)
    assert sha(root / p) == h, p
source_doc = "src/tushenghao/docs/ref/G4-Codex.md"
initial = json.loads((E / "initial-tracked-sha256.json").read_text())
assert sha(root / source_doc) == initial[source_doc]
assert (E / "frozen-g2.diff").stat().st_size == 0
assert "validated_time_bound_s_.value()" not in (root / "src/tushenghao/lib/tracking/target_tracker.cpp").read_text()
assert "validated_time_bound_s_.value()" not in (root / "src/tushenghao/tools/validation/tracking_lab.cpp").read_text()
assert "G4_PROFILE_BOUNDED_TIME_BOUND_MISSING" in (root / "src/tushenghao/lib/config/tracking_config.cpp").read_text()
S = json.loads((E / "results-final.json").read_text())
mapping = dict(association_nis="association_nis_limit", bootstrap_nis="bootstrap_nis_limit",
               position_std_m="position_std_limit_m", velocity_std_mps="velocity_std_limit_mps",
               max_dt_s="max_dt_s", time_bound_reference_s="validated_time_bound_s")
for tag, d in S["domains"].items():
    for stat in d["statistics"]:
        assert math.isclose(stat["threshold"], float(d["profile"][mapping[stat["statistic"]]]), rel_tol=1e-12)
    assert len(d["session_layers"]) == 12
    assert len(list((E / (tag+"-holdout")).glob("holdout-seed-*-tracks.yaml"))) == 3
    assert (E / (tag+"-replay/effective_profile.yaml")).read_bytes() == (E / (tag+"-calibration/profile.yaml")).read_bytes()
datasets = {}
for dataset in ["data-14ms", "data-33ms"]:
    manifest = (E / dataset / "manifest.yaml").read_text()
    splits = re.findall(r'^\s+split: (\w+)$', manifest, re.M)
    assert {s: splits.count(s) for s in set(splits)} == {"train":3,"selection":2,"holdout":3}
    names = re.findall(r'^\s+name: (\S+)$', manifest, re.M)
    assert len(names) == len(set(names)) == 8
    frames = re.findall(r'^\s+frames: "([^"]+)"$', manifest, re.M)
    truths = re.findall(r'^\s+truth: "([^"]+)"$', manifest, re.M)
    assert len(frames) == len(truths) == 8
    counts = {}
    for f, t in zip(frames, truths):
        assert (E / dataset / f).is_file() and (E / dataset / t).is_file()
        with (E / dataset / t).open() as source:
            n = sum(1 for _ in csv.DictReader(source))
        counts[f] = n
    datasets[dataset] = counts
future = {str((E/p).resolve()) for p in ["g4-delivery.zip", "g4-delivery.zip.sha256", "package-report.json", "archive-audit.json"]}
missing = []
checked = 0
for p in [root/"src/tushenghao/README.md",root/"src/tushenghao/docs/INDEX.md",
          root/"src/tushenghao/docs/g4.md",root/"src/tushenghao/docs/g4_acceptance.md",E/"INDEX.md",E/"commands.md"]:
    for ref in re.findall(r'\[[^\]]*\]\(([^)]+)\)', p.read_text()):
        if "://" in ref or ref.startswith("#"):
            continue
        target = (p.parent / unquote(ref.split("#",1)[0])).resolve()
        if not target.exists() and str(target) not in future:
            missing.append({"from":str(p.relative_to(root)),"target":ref})
        checked += 1
assert not missing, ("STOP: missing documentation target", missing)
for p in [root/"src/tushenghao/docs/g4.md",root/"src/tushenghao/docs/g4_acceptance.md"]:
    text=p.read_text()
    assert "未执行（由本地执行结果更新）" not in text
    for gate in ["G-01","G-02","G-03"]:
        assert gate in text and "OPEN" in text
assert (E / "after-final.patch").read_bytes() != (E / "before.patch").read_bytes()
result = dict(exit_code=0, modified_files=23, new_files=20, source_snapshot_verified=True,
              raw_sha256_verified=True, datasets=datasets, doc_links_checked=checked,
              missing_doc_targets=[], statistics_equal_profile=True, gates=S["gates"])
(E/"prepackage-audit.json").write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n")
print(json.dumps(result, ensure_ascii=False, indent=2))
