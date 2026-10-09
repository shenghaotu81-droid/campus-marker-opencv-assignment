"""旧实验依赖临时目录；本轮把每次完整运行、输入身份和逐值对账固定归档。"""

import hashlib
import json
import math
import platform
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
SOURCE = ROOT / "src/tushenghao"
ARCHIVE = SOURCE / "docs/evidence/112-recovery"


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def records(path):
    rows = [json.loads(line) for line in path.read_text().splitlines()]
    if [row["frame"] for row in rows] != list(range(1676)):
        raise ValueError(f"incomplete or unordered video: {path}")
    return rows


def compare(rows, baseline):
    targets = set(json.loads((SOURCE / "docs/evidence/112-forensics/deep/"
                            "sample_definition.json").read_text())["failed_ids"])
    added, lost, changed, outside_status = [], [], [], []
    for old, new in zip(baseline, rows):
        frame = new["frame"]
        if old["detections"]:
            if not new["detections"]:
                lost.append(frame)
            if old["status"] != new["status"] or old["detections"] != new["detections"]:
                changed.append(frame)
        elif new["detections"]:
            added.append(frame)
        if frame not in targets and old["status"] != new["status"]:
            outside_status.append(frame)
    return {"added_ids": added, "recovered": len(set(added) & targets),
            "remaining_ids": sorted(targets - set(added)), "lost_ids": lost,
            "original_raw_changes": changed, "outside_added_ids": sorted(set(added) - targets),
            "outside_status_changes": outside_status}


def distribution(rows):
    elapsed = sorted(row["elapsed_ms"] for row in rows)
    if not all(math.isfinite(value) and value >= 0 for value in elapsed):
        raise ValueError("invalid timing")
    return {"frames": len(rows), "detected": sum(bool(row["detections"]) for row in rows),
            "detections_total": sum(len(row["detections"]) for row in rows),
            "mean_ms": sum(elapsed) / len(elapsed),
            **{name: elapsed[math.ceil(rank * len(elapsed)) - 1]
               for name, rank in (("p50_ms", .5), ("p95_ms", .95), ("p99_ms", .99))},
            "max_ms": elapsed[-1], "over33": sum(value > 33 for value in elapsed)}


def run(label, executable, expected):
    destination = ARCHIVE / label
    destination.mkdir(parents=True, exist_ok=False)
    executable = Path(executable).resolve()
    # 缓存验收必须绑定真实源码；逐步快照让最终重写后仍能复核每一步。
    inputs = [ROOT / "data/raw/marker_video.avi", SOURCE / "CMakeLists.txt"]
    inputs.append(SOURCE / "docs/evidence/112-forensics/deep/sample_definition.json")
    for directory in ("lib", "include", "config"):
        inputs.extend(sorted((SOURCE / directory).rglob("*.*")))
    inputs.extend(sorted((SOURCE / "tools/validation").glob("recovery112_*")))
    hashes = {str(path.relative_to(ROOT)): digest(path) for path in inputs if path.is_file()}
    for path in sorted((SOURCE / "lib").rglob("*.*")):
        if path.is_file():
            target = destination / "source" / path.relative_to(SOURCE)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
    command = [str(executable), "src/tushenghao/config/detector_verification.yaml",
               "data/raw/marker_video.avi"]
    manifest = {"label": label, "started_utc": datetime.now(timezone.utc).isoformat(),
                "command": command, "binary_sha256": digest(executable), "inputs": hashes,
                "platform": platform.platform(), "threads": 1, "build_type": "Release",
                "timing": "steady_clock public process including Summary; decode/JSON excluded"}
    with (destination / "frames.jsonl").open("w") as output, \
            (destination / "run.log").open("w") as log:
        result = subprocess.run(command, cwd=ROOT, stdout=output, stderr=log, check=False)
    manifest["exit_code"] = result.returncode
    manifest["ended_utc"] = datetime.now(timezone.utc).isoformat()
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    if result.returncode:
        raise RuntimeError(f"video failed; preserved in {destination}")
    rows = records(destination / "frames.jsonl")
    summary = distribution(rows)
    if label != "baseline":
        summary.update(compare(rows, records(ARCHIVE / "baseline/frames.jsonl")))
    manifest["outputs"] = {name: digest(destination / name)
                           for name in ("frames.jsonl", "run.log")}
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (destination / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, ensure_ascii=False))
    if summary["detected"] != expected or any(summary.get(key) for key in
            ("lost_ids", "original_raw_changes", "outside_added_ids", "outside_status_changes")):
        raise AssertionError("video acceptance failed")


def summarize():
    # 全帧/旧成功/集合外/长尾分别断言；未达标探索运行也保留，不能只汇总最好的样本。
    expected = {"baseline": 976, "step1-index": 976, "step2-search": 976, "step3-ab": 996,
                "step4-periodic": 1088, "step6-first": 1088, "step6-scratch": 1088,
                "step6-model-scratch": 1088, "final-repeat1": 1088, "final-repeat2": 1088}
    baseline = records(ARCHIVE / "baseline/frames.jsonl")
    result = {"videos": {}}
    for label, count in expected.items():
        directory = ARCHIVE / label
        rows = records(directory / "frames.jsonl")
        summary = distribution(rows)
        summary.update(compare(rows, baseline))
        assert summary["detected"] == count == summary["detections_total"]
        assert not any(summary[key] for key in ("lost_ids", "original_raw_changes",
                                               "outside_added_ids", "outside_status_changes"))
        if label in ("step1-index", "step2-search"):
            assert [(r["status"], r["detections"]) for r in rows] == \
                   [(r["status"], r["detections"]) for r in baseline]
        if label.startswith("final-"):
            assert summary["recovered"] == 112 and not summary["remaining_ids"]
            assert summary["mean_ms"] <= 25 and summary["max_ms"] <= 33 and summary["over33"] == 0
        manifest = json.loads((directory / "manifest.json").read_text())
        for name, value in manifest["outputs"].items():
            assert digest(directory / name) == value
        for name, value in manifest["inputs"].items():
            if name.startswith("src/tushenghao/lib/"):
                assert digest(directory / "source" / name.removeprefix("src/tushenghao/")) == value
        result["videos"][label] = summary

    first = records(ARCHIVE / "final-repeat1/frames.jsonl")
    second = records(ARCHIVE / "final-repeat2/frames.jsonl")
    assert [(r["status"], r["detections"]) for r in first] == \
           [(r["status"], r["detections"]) for r in second]
    previous = records(ARCHIVE / "step4-periodic/frames.jsonl")
    changes = []
    for old, new in zip(previous, first):
        assert old["status"] == new["status"]
        if old["detections"] != new["detections"]:
            delta = max(math.hypot(a[0] - b[0], a[1] - b[1])
                        for old_detection, new_detection in zip(old["detections"], new["detections"])
                        for a, b in zip(old_detection, new_detection))
            changes.append({"frame": new["frame"], "max_corner_delta_px": delta})
    result["priority_changes"] = changes
    result["priority_max_delta_px"] = max(change["max_corner_delta_px"] for change in changes)
    result["repeat_raw_equal"] = True
    historical = records(SOURCE / "docs/evidence/112-worstframe/final-repeat1.jsonl")
    result["historical_fast_raw_equal"] = \
        [(r["status"], r["detections"]) for r in first] == \
        [(r["status"], r["detections"]) for r in historical]

    result["synthetic"] = {}
    for split, seed, positive_count, accepted, negative_count in \
            (("calibration", 112, 25, 24, 0), ("test", 7919, 68, 67, 20)):
        rows = [json.loads(line) for line in (ARCHIVE / f"final-{split}.jsonl").read_text().splitlines()]
        historical_rows = [json.loads(line) for line in
                           (SOURCE / f"docs/evidence/112-worstframe/independent-{split}.jsonl")
                           .read_text().splitlines()]
        identity = lambda row: tuple(row[key] for key in ("id", "angle", "amplitude", "local", "negative"))
        assert list(map(identity, rows)) == list(map(identity, historical_rows))
        positives = [row for row in rows if not row["negative"]]
        negatives = [row for row in rows if row["negative"]]
        passed = [row for row in positives if row["valid"]]
        modeled = [row for row in passed if row["modeled_edges"]]
        rejected = [row["id"] for row in positives if not row["valid"]]
        assert len(positives) == positive_count and len(passed) == accepted
        assert len(negatives) == negative_count and not any(row["valid"] for row in negatives)
        assert rejected == [2] and all(row["tamper_rejected"] for row in modeled)
        assert max(row["max_truth_error"] for row in passed) < 2
        result["synthetic"][split] = {"seed": seed, "samples": len(rows),
                                     "positives": positive_count, "accepted": accepted,
                                     "negative_rejected": len(negatives), "rejected_positive_ids": rejected,
                                     "max_truth_error_px": max(row["max_truth_error"] for row in passed),
                                     "actual_tamper_checks": len(modeled), "tamper_all_rejected": True,
                                     "fixed_sample_identity_equal": True}

    result["ctest"] = {}
    for build in ("release", "debug"):
        log = ARCHIVE / f"final-{build}-ctest.log"
        assert "100% tests passed, 0 tests failed out of 25" in log.read_text()
        flags = (ARCHIVE / "build-metadata" / build / "flags.make").read_text()
        assert ("-DNDEBUG" in flags) == (build == "release")
        result["ctest"][build] = {"passed": 25, "total": 25, "assertions_enabled": build == "debug"}
    protected = json.loads((ARCHIVE / "protected-before.json").read_text())
    for name, value in protected.items():
        if name != "src/tushenghao/README.md":
            assert digest(ROOT / name) == value
    assert digest(SOURCE / "docs/history/README_before_112_recovery.md") == protected["src/tushenghao/README.md"]
    result["public_config_ref_unchanged"] = True
    result["readme_original_byte_equal"] = True
    result["reference_comparisons"] = 39
    assert "reference comparisons=39 (all arcs/pairs/evidence exact)" in \
           (ARCHIVE / "final-reference.log").read_text()
    result["all_acceptance_passed"] = True
    (ARCHIVE / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({key: value for key, value in result.items() if key != "videos"}, ensure_ascii=False))


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "summarize":
        summarize()
    elif len(sys.argv) == 5 and sys.argv[1] == "run":
        run(sys.argv[2], sys.argv[3], int(sys.argv[4]))
    else:
        raise SystemExit("usage: recovery112_check.py run LABEL BINARY EXPECTED_DETECTED | summarize")
