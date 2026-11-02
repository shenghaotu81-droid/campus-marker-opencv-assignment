"""旧实验依赖临时目录；本轮把每次完整运行、输入身份和逐值对账固定归档。"""

import argparse
import re
import hashlib
import json
import math
import platform
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


# V03/V04：证据条件在 python -O 下也必须执行，失败保留具体条件位置。
def require(condition, message):
    if not condition:
        raise AssertionError(message)

# V06：路径与冻结验收定义都由入口提供，缺输入立即失败，不创建成功产物。
def load_paths(arguments):
    arguments.source_root = arguments.source_root.resolve(strict=True)
    arguments.build_logs = arguments.build_logs.resolve(strict=True)
    arguments.output_root = arguments.output_root.resolve()
    arguments.input_manifest = arguments.input_manifest.resolve(strict=True)
    arguments.manifest = json.loads(arguments.input_manifest.read_text())
    required = ("input_root", "video", "config", "failed_ids_file", "expected_frames",
                "expected_runs", "expected_test_targets", "historical_fast", "historical_synthetic",
                "protected_before", "readme_current", "readme_original", "legacy_source_prefix")
    for key in required:
        if key not in arguments.manifest:
            raise ValueError(f"input_manifest missing {key}")
    arguments.input_root = Path(arguments.manifest["input_root"]).resolve(strict=True)
    for key in ("video", "config", "failed_ids_file", "historical_fast", "protected_before", "readme_original"):
        Path(arguments.manifest[key]).resolve(strict=True)
    if not arguments.manifest["expected_test_targets"]:
        raise ValueError("expected_test_targets must name the required tests")
    return arguments


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def records(path, expected_frames):
    rows = [json.loads(line) for line in path.read_text().splitlines()]
    if [row["frame"] for row in rows] != list(range(expected_frames)):
        raise ValueError(f"incomplete or unordered video: {path}")
    return rows


def compare(rows, baseline, paths):
    targets = set(json.loads(Path(paths.manifest["failed_ids_file"]).read_text())["failed_ids"])
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


def run(label, executable, expected, paths):
    destination = paths.output_root / label
    destination.mkdir(parents=True, exist_ok=False)
    executable = Path(executable).resolve()
    # 缓存验收必须绑定真实源码；逐步快照让最终重写后仍能复核每一步。
    inputs = [Path(paths.manifest["video"]), paths.source_root / "CMakeLists.txt"]
    inputs.append(Path(paths.manifest["failed_ids_file"]))
    for directory in ("src", "include", "config"):
        inputs.extend(sorted((paths.source_root / directory).rglob("*.*")))
    inputs.extend(sorted((paths.source_root / "tools/validation/recovery").glob("recovery112_*")))
    hashes = {str(path.resolve()): digest(path) for path in inputs if path.is_file()}
    for path in sorted((paths.source_root / "src").rglob("*.*")):
        if path.is_file():
            target = destination / "source" / path.relative_to(paths.source_root)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
    command = [str(executable), str(Path(paths.manifest["config"]).resolve()),
               str(Path(paths.manifest["video"]).resolve())]
    manifest = {"label": label, "started_utc": datetime.now(timezone.utc).isoformat(),
                "command": command, "binary_sha256": digest(executable), "inputs": hashes,
                "platform": platform.platform(), "threads": 1, "build_type": "Release",
                "timing": "steady_clock public process including Summary; decode/JSON excluded"}
    with (destination / "frames.jsonl").open("w") as output, \
            (destination / "run.log").open("w") as log:
        result = subprocess.run(command, cwd=paths.input_root, stdout=output, stderr=log, check=False)
    manifest["exit_code"] = result.returncode
    manifest["ended_utc"] = datetime.now(timezone.utc).isoformat()
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    if result.returncode:
        raise RuntimeError(f"video failed; preserved in {destination}")
    rows = records(destination / "frames.jsonl", paths.manifest["expected_frames"])
    summary = distribution(rows)
    if label != "baseline":
        summary.update(compare(rows, records(paths.output_root / "baseline/frames.jsonl", paths.manifest["expected_frames"]), paths))
    manifest["outputs"] = {name: digest(destination / name)
                           for name in ("frames.jsonl", "run.log")}
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (destination / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, ensure_ascii=False))
    if summary["detected"] != expected or any(summary.get(key) for key in
            ("lost_ids", "original_raw_changes", "outside_added_ids", "outside_status_changes")):
        raise AssertionError("video acceptance failed")


# B06：按明确目标集验收，允许日志包含新目标，不固定测试总数。
def check_ctest(log, flags_path, build, expected_targets):
    text = log.read_text()
    total = re.search(r"100% tests passed, 0 tests failed out of (\d+)", text)
    require(total is not None, f"CTest failed or summary missing: {log}")
    passed = set(re.findall(r"Test\s+#\d+:\s+(\S+)\s+\.+\s+Passed", text))
    require(set(expected_targets) <= passed,
            f"required CTest targets missing: {log}")
    flags = flags_path.read_text()
    require(("-DNDEBUG" in flags) == (build == "release"), f"build mode mismatch: {build}")
    return {"passed": int(total[1]), "total": int(total[1]),
                              "assertions_enabled": build == "debug"}


def summarize(paths):
    # 全帧/旧成功/集合外/长尾分别断言；未达标探索运行也保留，不能只汇总最好的样本。
    expected = paths.manifest["expected_runs"]
    baseline = records(paths.output_root / "baseline/frames.jsonl", paths.manifest["expected_frames"])
    result = {"videos": {}}
    for label, count in expected.items():
        directory = paths.output_root / label
        rows = records(directory / "frames.jsonl", paths.manifest["expected_frames"])
        summary = distribution(rows)
        summary.update(compare(rows, baseline, paths))
        require(summary['detected'] == count == summary['detections_total'], "recovery112_check.py:122: summary['detected'] == count == summary['detections_total']")
        require(not any((summary[key] for key in ('lost_ids', 'original_raw_changes', 'outside_added_ids', 'outside_status_changes'))), "recovery112_check.py:123: not any((summary[key] for key in ('lost_ids', 'original_raw_changes', 'outside_added_ids', 'outside_status_changes')))")
        if label in ("step1-index", "step2-search"):
            require([(r['status'], r['detections']) for r in rows] == [(r['status'], r['detections']) for r in baseline], "recovery112_check.py:126: [(r['status'], r['detections']) for r in rows] == [(r['status'], r['detections']) for r in baseline]")
        if label.startswith("final-"):
            require(summary['recovered'] == 112 and (not summary['remaining_ids']), "recovery112_check.py:129: summary['recovered'] == 112 and (not summary['remaining_ids'])")
            require(summary['mean_ms'] <= 25 and summary['max_ms'] <= 33 and (summary['over33'] == 0), "recovery112_check.py:130: summary['mean_ms'] <= 25 and summary['max_ms'] <= 33 and (summary['over33'] == 0)")
        manifest = json.loads((directory / "manifest.json").read_text())
        for name, value in manifest["outputs"].items():
            require(digest(directory / name) == value, 'recovery112_check.py:133: digest(directory / name) == value')
        for name, value in manifest["inputs"].items():
            if name.startswith(str(paths.source_root / "src") + "/") or name.startswith(paths.manifest["legacy_source_prefix"] + "lib/"):
                relative = (Path(name).relative_to(paths.source_root) if Path(name).is_absolute()
                            else Path(name.removeprefix(paths.manifest["legacy_source_prefix"])))
                require(digest(directory / "source" / relative) == value, f"source snapshot hash mismatch: {name}")
        result["videos"][label] = summary

    first = records(paths.output_root / "final-repeat1/frames.jsonl", paths.manifest["expected_frames"])
    second = records(paths.output_root / "final-repeat2/frames.jsonl", paths.manifest["expected_frames"])
    require([(r['status'], r['detections']) for r in first] == [(r['status'], r['detections']) for r in second], "recovery112_check.py:141: [(r['status'], r['detections']) for r in first] == [(r['status'], r['detections']) for r in second]")
    previous = records(paths.output_root / "step4-periodic/frames.jsonl", paths.manifest["expected_frames"])
    changes = []
    for old, new in zip(previous, first):
        require(old['status'] == new['status'], "recovery112_check.py:146: old['status'] == new['status']")
        if old["detections"] != new["detections"]:
            delta = max(math.hypot(a[0] - b[0], a[1] - b[1])
                        for old_detection, new_detection in zip(old["detections"], new["detections"])
                        for a, b in zip(old_detection, new_detection))
            changes.append({"frame": new["frame"], "max_corner_delta_px": delta})
    result["priority_changes"] = changes
    result["priority_max_delta_px"] = max(change["max_corner_delta_px"] for change in changes)
    result["repeat_raw_equal"] = True
    historical = records(Path(paths.manifest["historical_fast"]), paths.manifest["expected_frames"])
    result["historical_fast_raw_equal"] = \
        [(r["status"], r["detections"]) for r in first] == \
        [(r["status"], r["detections"]) for r in historical]

    result["synthetic"] = {}
    for split, seed, positive_count, accepted, negative_count in \
            (("calibration", 112, 25, 24, 0), ("test", 7919, 68, 67, 20)):
        rows = [json.loads(line) for line in (paths.output_root / f"final-{split}.jsonl").read_text().splitlines()]
        historical_rows = [json.loads(line) for line in
                           Path(paths.manifest["historical_synthetic"][split])
                           .read_text().splitlines()]
        identity = lambda row: tuple(row[key] for key in ("id", "angle", "amplitude", "local", "negative"))
        require(list(map(identity, rows)) == list(map(identity, historical_rows)), 'recovery112_check.py:168: list(map(identity, rows)) == list(map(identity, historical_rows))')
        positives = [row for row in rows if not row["negative"]]
        negatives = [row for row in rows if row["negative"]]
        passed = [row for row in positives if row["valid"]]
        modeled = [row for row in passed if row["modeled_edges"]]
        rejected = [row["id"] for row in positives if not row["valid"]]
        require(len(positives) == positive_count and len(passed) == accepted, 'recovery112_check.py:174: len(positives) == positive_count and len(passed) == accepted')
        require(len(negatives) == negative_count and (not any((row['valid'] for row in negatives))), "recovery112_check.py:175: len(negatives) == negative_count and (not any((row['valid'] for row in negatives)))")
        require(rejected == [2] and all((row['tamper_rejected'] for row in modeled)), "recovery112_check.py:176: rejected == [2] and all((row['tamper_rejected'] for row in modeled))")
        require(max((row['max_truth_error'] for row in passed)) < 2, "recovery112_check.py:177: max((row['max_truth_error'] for row in passed)) < 2")
        result["synthetic"][split] = {"seed": seed, "samples": len(rows),
                                     "positives": positive_count, "accepted": accepted,
                                     "negative_rejected": len(negatives), "rejected_positive_ids": rejected,
                                     "max_truth_error_px": max(row["max_truth_error"] for row in passed),
                                     "actual_tamper_checks": len(modeled), "tamper_all_rejected": True,
                                     "fixed_sample_identity_equal": True}

    result["ctest"] = {}
    for build in ("release", "debug"):
        log = paths.build_logs / f"final-{build}-ctest.log"
        result["ctest"][build] = check_ctest(log, paths.build_logs / "build-metadata" / build / "flags.make",
                                               build, paths.manifest["expected_test_targets"])
    protected = json.loads(Path(paths.manifest["protected_before"]).read_text())
    for name, value in protected.items():
        if name != paths.manifest["readme_current"]:
            require(digest(paths.input_root / name) == value, 'recovery112_check.py:195: digest(paths.input_root / name) == value')
    require(digest(Path(paths.manifest["readme_original"])) == protected[paths.manifest["readme_current"]], "historical README bytes differ")
    result["public_config_ref_unchanged"] = True
    result["readme_original_byte_equal"] = True
    result["reference_comparisons"] = 39
    require('reference comparisons=39 (all arcs/pairs/evidence exact)' in (paths.output_root / 'final-reference.log').read_text(), "recovery112_check.py:200: 'reference comparisons=39 (all arcs/pairs/evidence exact)' in (paths.output_root / 'final-reference.log').read_text()")
    result["all_acceptance_passed"] = True
    (paths.output_root / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({key: value for key, value in result.items() if key != "videos"}, ensure_ascii=False))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="显式输入的 112 回放验收；历史原文只读")
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--input-manifest", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--build-logs", type=Path, required=True)
    commands = parser.add_subparsers(dest="command", required=True)
    replay = commands.add_parser("run")
    replay.add_argument("label")
    replay.add_argument("executable")
    replay.add_argument("expected", type=int)
    commands.add_parser("summarize")
    paths = load_paths(parser.parse_args())
    if paths.command == "summarize":
        summarize(paths)
    else:
        run(paths.label, paths.executable, paths.expected, paths)
