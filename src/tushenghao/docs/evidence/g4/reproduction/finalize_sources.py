from pathlib import Path
import difflib
import hashlib
import json
import re
import shutil
import subprocess

root = Path(__file__).resolve().parents[6]
E = root / "src/tushenghao/docs/evidence/g4"
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def git(*args):
    return subprocess.check_output(["git", *args], cwd=root)
initial = json.loads((E / "initial-tracked-sha256.json").read_text())
allowed = ["src/tushenghao/CMakeLists.txt", "src/tushenghao/README.md", "src/tushenghao/docs/INDEX.md"]
changed = [p for p, h in initial.items() if not (root / p).is_file() or digest(root / p) != h]
assert sorted(changed) == sorted(allowed), ("STOP: unexpected original file changes", changed)
additions = {
    allowed[0]: "# G4独立感知估计层，原G2保持冻结。\ninclude(cmake/g4.cmake)\n",
    allowed[1]: "## G4 帧间跟踪与速度估计\n\n构建/运行/参数与[后续关闭流程](docs/g4.md#后续关闭流程)见[G4使用说明](docs/g4.md)，软件实测与三项OPEN关卡见[G4验收记录](docs/g4_acceptance.md)。本期只感知，不做控制；G2当前无真实Unique、速度误差界和有界时间仍待实测，未经过实车确认。\n\n",
    allowed[2]: "\n## G4 动态目标估计\n\n- [G4使用说明与后续关闭流程](g4.md)\n- [G4实施验收记录](g4_acceptance.md)\n- [G4 Codex执行依据](ref/G4-Codex.md)\n- 本期证据：`evidence/g4/`；编译产物留在build，不归档到docs。\n",
}
for p, addition in additions.items():
    text = (root / p).read_text()
    assert text.count(addition) == 1
    restored = text.replace(addition, "", 1).encode()
    assert hashlib.sha256(restored).hexdigest() == initial[p], ("STOP: non-append integration change", p)
assert (E / "frozen-g2.diff").stat().st_size == 0
assert git("diff", "--cached", "--", "src/tushenghao") == (E / "initial-index.diff").read_bytes()
assert git("branch", "--show-current").decode().strip() == "feat/tracking"
assert git("rev-parse", "HEAD").decode().strip() == "30ac7ab43e3c54576fbeba5264d66f5d61d6bad9"
pose = (root / "src/tushenghao/config/pose.yaml").read_text()
assert 'min_quad_area_px2: ""' in pose and 'max_time_uncertainty_s: ""' in pose
files = (E / "new-files.txt").read_text().splitlines()
assert len(files) == 20 and all((root / p).is_file() for p in files)
all_modified = files + allowed
(E / "modified-files.txt").write_text("\n".join(all_modified) + "\n")
(E / "new-files.sha256").write_text("".join(digest(root / p) + "  " + p + "\n" for p in files))
(E / "modified-files.sha256").write_text("".join(digest(root / p) + "  " + p + "\n" for p in all_modified))
(E / "frozen-originals.sha256").write_text("".join(h + "  " + p + "\n" for p, h in initial.items() if p not in allowed))
freeze = dict(initial_files=len(initial), unchanged_original_files=len(initial)-3,
              modified_original_files=allowed, exact_specified_insertions=True,
              frozen_g2_diff_bytes=0, initial_index_diff_preserved=True, head_and_branch_preserved=True,
              empty_pose_budgets_preserved=True, new_files=20, modified_files=23,
              preexisting_user_file="src/tushenghao/docs/ref/G4-Codex.md preserved byte-for-byte",
              git_operations="read-only branch/rev-parse/status/diff/ls-files only")
(E / "freeze-check.json").write_text(json.dumps(freeze, ensure_ascii=False, indent=2) + "\n")
snapshot = E / "source-snapshot"
assert not snapshot.exists()
for p in all_modified + ["src/tushenghao/docs/ref/G4-Codex.md"]:
    target = snapshot / p
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(root / p, target)
    assert digest(target) == digest(root / p)
new_patch = "".join("diff --git a/"+p+" b/"+p+"\nnew file mode 100644\n" +
                    "".join(difflib.unified_diff([], (root/p).read_text().splitlines(True),
                                               fromfile="/dev/null", tofile="b/"+p)) for p in files)
(E / "new-files.patch").write_text(new_patch)
incremental = git("diff", "30ac7ab43e3c54576fbeba5264d66f5d61d6bad9", "--", *allowed)
(E / "incremental-tracked.patch").write_bytes(incremental)
(E / "delivery.patch").write_bytes(incremental + new_patch.encode())
doc = (root / "src/tushenghao/docs/ref/G4-Codex.md").read_text()
heads = list(re.finditer(r'^### 4\.(\d+) `([^`]+)`\n', doc, re.M))
deviations = []
for i, h in enumerate(heads):
    part = doc[h.end():heads[i+1].start() if i+1 < len(heads) else doc.index("\n## 5.", h.end())]
    fence = re.search(r'^(`{3,4})\w+\n', part, re.M)
    end = re.search(r'^'+fence[1]+r'\s*$', part[fence.end():], re.M)
    body = part[fence.end():fence.end()+end.start()]
    current = (root / h[2]).read_text()
    if current != body:
        deviations.append(h[2])
    with (E / "document-deviations.patch").open("a") as f:
        f.writelines(difflib.unified_diff(body.splitlines(True), current.splitlines(True),
                                        fromfile="document/"+h[2], tofile="working/"+h[2]))
assert set(deviations) == {
    "src/tushenghao/lib/config/tracking_config.cpp", "src/tushenghao/lib/tracking/target_tracker.cpp",
    "src/tushenghao/tools/validation/tracking_lab.cpp", "src/tushenghao/tests/tracking_contract_test.cpp",
    "src/tushenghao/docs/g4.md", "src/tushenghao/docs/g4_acceptance.md"}, deviations
print(json.dumps(freeze, ensure_ascii=False, indent=2))
print("20-file hashes, 23-file change list, complete source snapshot and patches archived; original files protected")
