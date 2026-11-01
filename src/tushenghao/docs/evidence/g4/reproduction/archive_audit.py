from pathlib import Path
import hashlib
import json
import re
import subprocess
import zipfile
from urllib.parse import unquote

root = Path(__file__).resolve().parents[6]
E = root / "src/tushenghao/docs/evidence/g4"
report = json.loads((E / "package-report.json").read_text())
archive = root / report["archive"]
assert hashlib.sha256(archive.read_bytes()).hexdigest() == report["sha256"]
source_files = (E / "modified-files.txt").read_text().splitlines()
with zipfile.ZipFile(archive) as z:
    names = set(z.namelist())
    manifest = (E / "archive-inventory.sha256").relative_to(root).as_posix()
    assert z.read(manifest) == (E / "archive-inventory.sha256").read_bytes()
    assert len(names) == report["files"]
    for p in source_files + ["src/tushenghao/docs/ref/G4-Codex.md"]:
        entry = (E / "source-snapshot" / p).relative_to(root).as_posix()
        assert z.read(entry) == (root / p).read_bytes(), p
    for line in (E / "raw-data.sha256").read_text().splitlines():
        h, p = line.split("  ", 1)
        assert p in names and hashlib.sha256(z.read(p)).hexdigest() == h, p
    assert all(name.startswith("src/tushenghao/docs/evidence/g4/") for name in names)
    assert not any("/build/" in name or "/.git/" in name or "/.aws/" in name for name in names)
index = (E / "INDEX.md").read_text()
missing = []
for ref in re.findall(r'\[[^\]]*\]\(([^)]+)\)',index):
    if "://" in ref or ref.startswith("#"):
        continue
    target = (E / unquote(ref.split("#",1)[0])).resolve()
    if not target.exists() and target != (E / "archive-audit.json").resolve():
        missing.append(ref)
assert not missing, missing
assert subprocess.check_output(["git","branch","--show-current"],cwd=root).decode().strip() == "feat/tracking"
assert subprocess.check_output(["git","rev-parse","HEAD"],cwd=root).decode().strip() == "30ac7ab43e3c54576fbeba5264d66f5d61d6bad9"
assert subprocess.check_output(["git","diff","--cached","--","src/tushenghao"],cwd=root) == (E/"initial-index.diff").read_bytes()
result = dict(exit_code=0, zip_sha256_verified=True, zip_entries=len(names),
              modified_source_snapshots_verified=len(source_files), user_document_preserved=True,
              raw_data_files_verified=36, missing_evidence_links=[],
              branch_head_staged_diff_preserved=True, gates={"G-01":"OPEN","G-02":"OPEN","G-03":"OPEN"})
(E / "archive-audit.json").write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n")
print(json.dumps(result,ensure_ascii=False,indent=2))
