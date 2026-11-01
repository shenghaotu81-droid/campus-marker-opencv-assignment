from pathlib import Path
import datetime
import hashlib
import json
import zipfile

root = Path(__file__).resolve().parents[6]
E = root / "src/tushenghao/docs/evidence/g4"
archive = E / "g4-delivery.zip"
partial = E / "g4-delivery.partial.zip"
assert not archive.exists() and not partial.exists(), "STOP: archive already exists"
excluded = {"package.log", "g4-delivery.zip", "g4-delivery.partial.zip", "g4-delivery.zip.sha256",
            "package-report.json", "archive-audit.json", "archive-inventory.sha256"}
paths = sorted(p for p in E.rglob("*") if p.is_file() and str(p.relative_to(E)) not in excluded)
assert all(not p.is_symlink() for p in paths)
entries = []
with zipfile.ZipFile(partial,"x",compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for p in paths:
        name = p.relative_to(root).as_posix()
        assert not any(part in {"build", ".git", ".aws", "third_party", "vendor"} for part in p.relative_to(E).parts)
        h = hashlib.sha256(p.read_bytes()).hexdigest()
        z.write(p, name)
        entries.append((name,h))
    inventory = "".join(h+"  "+name+"\n" for name,h in entries)
    z.writestr((E/"archive-inventory.sha256").relative_to(root).as_posix(),inventory)
with zipfile.ZipFile(partial) as z:
    assert z.testzip() is None, "STOP: ZIP CRC failure"
    for name,h in entries:
        assert hashlib.sha256(z.read(name)).hexdigest() == h, name
partial.rename(archive)
(E/"archive-inventory.sha256").write_text(inventory)
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
(E/"g4-delivery.zip.sha256").write_text(digest+"  "+archive.relative_to(root).as_posix()+"\n")
report = dict(exit_code=0, archive=archive.relative_to(root).as_posix(), sha256=digest,
              compressed_bytes=archive.stat().st_size, files=len(entries)+1,
              payload_bytes=sum(p.stat().st_size for p in paths), crc_and_all_entry_sha256_verified=True,
              includes_full_raw_data=True, includes_source_snapshot=True,
              excludes_build_third_party_credentials=True, excluded_package_sidecars=sorted(excluded),
              snapshot_time_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
              note="ZIP是打包时快照；打包/包外最终校验命令在外部commands.jsonl继续追加，archive-inventory按ZIP条目解释")
(E/"package-report.json").write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n")
print(json.dumps(report,ensure_ascii=False,indent=2))
